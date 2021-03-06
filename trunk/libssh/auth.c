/*
 * auth1.c - authentication with SSH protocols
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2003-2008 by Aris Adamantiadis
 * Copyright (c) 2008-2009 Andreas Schneider <mail@cynapses.org>
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#endif

#include "libssh/priv.h"
#include "libssh/ssh2.h"

/** \defgroup ssh_auth SSH Authentication functions
 * \brief functions to authenticate to servers
 */
/** \addtogroup ssh_auth
 * @{ */

static int ask_userauth(SSH_SESSION *session) {
  int rc = 0;

  enter_function();

  if (session->auth_service_asked) {
    rc = 0;
  } else if (ssh_service_request(session,"ssh-userauth")) {
    rc = -1;
  } else {
    session->auth_service_asked++;
  }

  leave_function();
  return rc;
}

static int wait_auth_status(SSH_SESSION *session, int kbdint) {
  char *auth_methods = NULL;
  STRING *auth;
  int rc = SSH_AUTH_ERROR;
  int cont = 1;
  u8 partial = 0;

  enter_function();

  while (cont) {
    if (packet_read(session) != SSH_OK) {
      break;
    }
    if (packet_translate(session) != SSH_OK) {
      break;
    }

    switch (session->in_packet.type) {
      case SSH2_MSG_USERAUTH_FAILURE:
        auth = buffer_get_ssh_string(session->in_buffer);
        if (auth == NULL || buffer_get_u8(session->in_buffer, &partial) != 1) {
          ssh_set_error(session, SSH_FATAL,
              "Invalid SSH_MSG_USERAUTH_FAILURE message");
          leave_function();
          return SSH_AUTH_ERROR;
        }

        auth_methods = string_to_char(auth);
        if (auth_methods == NULL) {
          ssh_set_error(session, SSH_FATAL,
              "Not enough space");
          string_free(auth);
          leave_function();
          return SSH_AUTH_ERROR;
        }

        if (partial) {
          rc = SSH_AUTH_PARTIAL;
          ssh_set_error(session, SSH_NO_ERROR,
              "Partial success. Authentication that can continue: %s",
              auth_methods);
        } else {
          rc = SSH_AUTH_DENIED;
          ssh_set_error(session, SSH_REQUEST_DENIED,
              "Access denied. Authentication that can continue: %s",
              auth_methods);

          session->auth_methods = 0;
          if (strstr(auth_methods, "password") != NULL) {
            session->auth_methods |= SSH_AUTH_METHOD_PASSWORD;
          }
          if (strstr(auth_methods, "keyboard-interactive") != NULL) {
            session->auth_methods |= SSH_AUTH_METHOD_INTERACTIVE;
          }
          if (strstr(auth_methods, "publickey") != NULL) {
            session->auth_methods |= SSH_AUTH_METHOD_PUBLICKEY;
          }
          if (strstr(auth_methods, "hostbased") != NULL) {
            session->auth_methods |= SSH_AUTH_METHOD_HOSTBASED;
          }
        }

        string_free(auth);
        SAFE_FREE(auth_methods);
        cont = 0;
        break;
      case SSH2_MSG_USERAUTH_PK_OK:
        /* SSH monkeys have defined the same number for both */
        /* SSH_MSG_USERAUTH_PK_OK and SSH_MSG_USERAUTH_INFO_REQUEST */
        /* which is not really smart; */
        /*case SSH2_MSG_USERAUTH_INFO_REQUEST: */
        if (kbdint) {
          rc = SSH_AUTH_INFO;
          cont = 0;
          break;
        }
        /* continue through success */
      case SSH2_MSG_USERAUTH_SUCCESS:
        rc = SSH_AUTH_SUCCESS;
        cont = 0;
        break;
      case SSH2_MSG_USERAUTH_BANNER:
        {
          STRING *banner;

          banner = buffer_get_ssh_string(session->in_buffer);
          if (banner == NULL) {
            ssh_log(session, SSH_LOG_PACKET,
                "The banner message was invalid. Continuing though\n");
            break;
          }
          ssh_log(session, SSH_LOG_PACKET,
              "Received a message banner\n");
          string_free(session->banner); /* erase the older one */
          session->banner = banner;
          break;
        }
      default:
        packet_parse(session);
        break;
    }
  }

  leave_function();
  return rc;
}

int ssh_auth_list(SSH_SESSION *session) {
  if (session == NULL) {
    return -1;
  }

  return session->auth_methods;
}

int ssh_userauth_list(SSH_SESSION *session, const char *username) {
  if (session == NULL || username == NULL) {
    return SSH_AUTH_ERROR;
  }

  if (session->auth_methods == 0) {
    ssh_userauth_none(session, username);
  }

  return ssh_auth_list(session);
}

/* use the "none" authentication question */

/**
 * @brief Try to authenticate through the "none" method.
 *
 * @param session       The ssh session to use.
 *
 * @param username      The username to authenticate. You can specify NULL if
 *                      ssh_option_set_username() has been used. You cannot try
 *                      two different logins in a row.
 *
 * @returns SSH_AUTH_ERROR:   A serious error happened.\n
 *          SSH_AUTH_DENIED:  Authentication failed: use another method\n
 *          SSH_AUTH_PARTIAL: You've been partially authenticated, you still
 *                            have to use another method\n
 *          SSH_AUTH_SUCCESS: Authentication success
 */
int ssh_userauth_none(SSH_SESSION *session, const char *username) {
  STRING *user = NULL;
  STRING *service = NULL;
  STRING *method = NULL;
  int rc = SSH_AUTH_ERROR;

  enter_function();

#ifdef HAVE_SSH1
  if (session->version == 1) {
    ssh_userauth1_none(session, username);
    leave_function();
    return rc;
  }
#endif

  if (username == NULL) {
    if (session->options->username == NULL) {
      if (ssh_options_default_username(session->options) < 0) {
        leave_function();
        return rc;
      }
    }
    user = string_from_char(session->options->username);
  } else {
    user = string_from_char(username);
  }

  if (user == NULL) {
    leave_function();
    return rc;
  }

  if (ask_userauth(session) < 0) {
    string_free(user);
    leave_function();
    return rc;
  }

  method = string_from_char("none");
  if (method == NULL) {
    goto error;
  }
  service = string_from_char("ssh-connection");
  if (service == NULL) {
    goto error;
  }

  if (buffer_add_u8(session->out_buffer, SSH2_MSG_USERAUTH_REQUEST) < 0 ||
      buffer_add_ssh_string(session->out_buffer, user) < 0 ||
      buffer_add_ssh_string(session->out_buffer, service) < 0 ||
      buffer_add_ssh_string(session->out_buffer, method) < 0) {
    goto error;
  }

  string_free(service);
  string_free(method);
  string_free(user);

  if (packet_send(session) != SSH_OK) {
    leave_function();
    return rc;
  }
  rc = wait_auth_status(session, 0);

  leave_function();
  return rc;
error:
  buffer_free(session->out_buffer);
  string_free(service);
  string_free(method);
  string_free(user);

  leave_function();
  return rc;
}

/**
 * @brief Try to authenticate through public key.
 *
 * @param session       The ssh session to use.
 *
 * @param username      The username to authenticate. You can specify NULL if
 *                      ssh_option_set_username() has been used. You cannot try
 *                      two different logins in a row.
 *
 * @param type          The type of the public key. This value is given by
 *                      publickey_from_file().
 *
 * @param publickey     A public key returned by publickey_from_file().
 *
 * @returns SSH_AUTH_ERROR:   A serious error happened.\n
 *          SSH_AUTH_DENIED:  The server doesn't accept that public key as an
 *                            authentication token. Try another key or another
 *                            method.\n
 *          SSH_AUTH_PARTIAL: You've been partially authenticated, you still
 *                            have to use another method.\n
 *          SSH_AUTH_SUCCESS: The public key is accepted, you want now to use
 *                            ssh_userauth_pubkey().
 *
 * @see publickey_from_file()
 * @see privatekey_from_file()
 * @see ssh_userauth_pubkey()
 */
int ssh_userauth_offer_pubkey(SSH_SESSION *session, const char *username,
    int type, STRING *publickey) {
  STRING *user = NULL;
  STRING *service = NULL;
  STRING *method = NULL;
  STRING *algo = NULL;
  int rc = SSH_AUTH_ERROR;

  enter_function();

#ifdef HAVE_SSH1
  if (session->version == 1) {
    ssh_userauth1_offer_pubkey(session, username, type, publickey);
    leave_function();
    return rc;
  }
#endif

  if (username == NULL) {
    if (session->options->username == NULL) {
      if (ssh_options_default_username(session->options) < 0) {
        leave_function();
        return rc;
      }
    }
    user = string_from_char(session->options->username);
  } else {
    user = string_from_char(username);
  }

  if (user == NULL) {
    leave_function();
    return rc;
  }

  if (ask_userauth(session) < 0) {
    string_free(user);
    leave_function();
    return rc;
  }

  service = string_from_char("ssh-connection");
  if (service == NULL) {
    goto error;
  }
  method = string_from_char("publickey");
  if (method == NULL) {
    goto error;
  }
  algo = string_from_char(ssh_type_to_char(type));
  if (algo == NULL) {
    goto error;
  }

  if (buffer_add_u8(session->out_buffer, SSH2_MSG_USERAUTH_REQUEST) < 0 ||
      buffer_add_ssh_string(session->out_buffer, user) < 0 ||
      buffer_add_ssh_string(session->out_buffer, service) < 0 ||
      buffer_add_ssh_string(session->out_buffer, method) < 0 ||
      buffer_add_u8(session->out_buffer, 0) < 0 ||
      buffer_add_ssh_string(session->out_buffer, algo) < 0 ||
      buffer_add_ssh_string(session->out_buffer, publickey) < 0) {
    goto error;
  }

  string_free(user);
  string_free(method);
  string_free(service);
  string_free(algo);

  if (packet_send(session) != SSH_OK) {
    leave_function();
    return rc;
  }
  rc = wait_auth_status(session,0);

  leave_function();
  return rc;
error:
  buffer_free(session->out_buffer);
  string_free(user);
  string_free(method);
  string_free(service);
  string_free(algo);

  leave_function();
  return rc;
}


/**
 * @brief Try to authenticate through public key.
 *
 * @param session       The ssh session to use.
 *
 * @param username      The username to authenticate. You can specify NULL if
 *                      ssh_option_set_username() has been used. You cannot try
 *                      two different logins in a row.
 *
 * @param publickey     A public key returned by publickey_from_file().
 *
 * @param privatekey    A private key returned by privatekey_from_file().
 *
 * @returns SSH_AUTH_ERROR:   A serious error happened.\n
 *          SSH_AUTH_DENIED:  Authentication failed: use another method.\n
 *          SSH_AUTH_PARTIAL: You've been partially authenticated, you still
 *                            have to use another method.\n
 *          SSH_AUTH_SUCCESS: Authentication successful.
 *
 * @see publickey_from_file()
 * @see privatekey_from_file()
 * @see privatekey_free()
 * @see ssh_userauth_offer_pubkey()
 */
int ssh_userauth_pubkey(SSH_SESSION *session, const char *username,
    STRING *publickey, PRIVATE_KEY *privatekey) {
  STRING *user = NULL;
  STRING *service = NULL;
  STRING *method = NULL;
  STRING *algo = NULL;
  STRING *sign = NULL;
  int rc = SSH_AUTH_ERROR;

  enter_function();

#if 0
  if (session->version == 1) {
    return ssh_userauth1_pubkey(session, username, publickey, privatekey);
  }
#endif

  if (username == NULL) {
    if (session->options->username == NULL) {
      if (ssh_options_default_username(session->options) < 0) {
        leave_function();
        return rc;
      }
    }
    user = string_from_char(session->options->username);
  } else {
    user = string_from_char(username);
  }

  if (user == NULL) {
    leave_function();
    return rc;
  }

  if (ask_userauth(session) < 0) {
    string_free(user);
    leave_function();
    return rc;
  }

  service = string_from_char("ssh-connection");
  if (service == NULL) {
    goto error;
  }
  method = string_from_char("publickey");
  if (method == NULL) {
    goto error;
  }
  algo = string_from_char(ssh_type_to_char(privatekey->type));
  if (algo == NULL) {
    goto error;
  }

  /* we said previously the public key was accepted */
  if (buffer_add_u8(session->out_buffer, SSH2_MSG_USERAUTH_REQUEST) < 0 ||
      buffer_add_ssh_string(session->out_buffer, user) < 0 ||
      buffer_add_ssh_string(session->out_buffer, service) < 0 ||
      buffer_add_ssh_string(session->out_buffer, method) < 0 ||
      buffer_add_u8(session->out_buffer, 1) < 0 ||
      buffer_add_ssh_string(session->out_buffer, algo) < 0 ||
      buffer_add_ssh_string(session->out_buffer, publickey) < 0) {
    goto error;
  }

  string_free(user);
  string_free(service);
  string_free(method);
  string_free(algo);

  sign = ssh_do_sign(session,session->out_buffer, privatekey);
  if (sign) {
    if (buffer_add_ssh_string(session->out_buffer,sign) < 0) {
      goto error;
    }
    string_free(sign);

    if (packet_send(session) != SSH_OK) {
      leave_function();
      return rc;
    }
    rc = wait_auth_status(session,0);
  }

  leave_function();
  return rc;
error:
  buffer_free(session->out_buffer);
  string_free(user);
  string_free(service);
  string_free(method);
  string_free(algo);

  leave_function();
  return rc;
}

#ifndef _WIN32
/**
 * @brief Try to authenticate through public key with an ssh agent.
 *
 * @param session       The ssh session to use.
 *
 * @param username      The username to authenticate. You can specify NULL if
 *                      ssh_option_set_username() has been used. You cannot try
 *                      two different logins in a row.
 *
 * @param publickey     The public key provided by the agent.
 *
 * @returns SSH_AUTH_ERROR:   A serious error happened.\n
 *          SSH_AUTH_DENIED:  Authentication failed: use another method.\n
 *          SSH_AUTH_PARTIAL: You've been partially authenticated, you still
 *                            have to use another method.\n
 *          SSH_AUTH_SUCCESS: Authentication successful.
 *
 * @see publickey_from_file()
 * @see privatekey_from_file()
 * @see privatekey_free()
 * @see ssh_userauth_offer_pubkey()
 */
int ssh_userauth_agent_pubkey(SSH_SESSION *session, const char *username,
    PUBLIC_KEY *publickey) {
  STRING *user = NULL;
  STRING *service = NULL;
  STRING *method = NULL;
  STRING *algo = NULL;
  STRING *key = NULL;
  STRING *sign = NULL;
  int rc = SSH_AUTH_ERROR;

  enter_function();

  if (! agent_is_running(session)) {
    return rc;
  }

  if (username == NULL) {
    if (session->options->username == NULL) {
      if (ssh_options_default_username(session->options) < 0) {
        leave_function();
        return rc;
      }
    }
    user = string_from_char(session->options->username);
  } else {
    user = string_from_char(username);
  }

  if (user == NULL) {
    leave_function();
    return rc;
  }

  if (ask_userauth(session) < 0) {
    string_free(user);
    leave_function();
    return rc;
  }

  service = string_from_char("ssh-connection");
  if (service == NULL) {
    goto error;
  }
  method = string_from_char("publickey");
  if (method == NULL) {
    goto error;
  }
  algo = string_from_char(ssh_type_to_char(publickey->type));
  if (algo == NULL) {
    goto error;
  }
  key = publickey_to_string(publickey);
  if (key == NULL) {
    goto error;
  }

  /* we said previously the public key was accepted */
  if (buffer_add_u8(session->out_buffer, SSH2_MSG_USERAUTH_REQUEST) < 0 ||
      buffer_add_ssh_string(session->out_buffer, user) < 0 ||
      buffer_add_ssh_string(session->out_buffer, service) < 0 ||
      buffer_add_ssh_string(session->out_buffer, method) < 0 ||
      buffer_add_u8(session->out_buffer, 1) < 0 ||
      buffer_add_ssh_string(session->out_buffer, algo) < 0 ||
      buffer_add_ssh_string(session->out_buffer, key) < 0) {
    goto error;
  }

  sign = ssh_do_sign_with_agent(session, session->out_buffer, publickey);

  if (sign) {
    if (buffer_add_ssh_string(session->out_buffer, sign) < 0) {
      goto error;
    }
    string_free(sign);
    if (packet_send(session) != SSH_OK) {
      leave_function();
      return rc;
    }
    rc = wait_auth_status(session,0);
  }

  string_free(user);
  string_free(service);
  string_free(method);
  string_free(algo);
  string_free(key);

  leave_function();

  return rc;
error:
  buffer_free(session->out_buffer);
  string_free(sign);
  string_free(user);
  string_free(service);
  string_free(method);
  string_free(algo);
  string_free(key);

  leave_function();
  return rc;
}
#endif /* _WIN32 */

/**
 * @brief Try to authenticate by password.
 *
 * @param session       The ssh session to use.
 *
 * @param username      The username to authenticate. You can specify NULL if
 *                      ssh_option_set_username() has been used. You cannot try
 *                      two different logins in a row.
 *
 * @param password      The password to use. Take care to clean it after
 *                      the authentication.
 *
 * @returns SSH_AUTH_ERROR:   A serious error happened.\n
 *          SSH_AUTH_DENIED:  Authentication failed: use another method.\n
 *          SSH_AUTH_PARTIAL: You've been partially authenticated, you still
 *                            have to use another method.\n
 *          SSH_AUTH_SUCCESS: Authentication successful.
 *
 * @see ssh_userauth_kbdint()
 * @see BURN_STRING
 */
int ssh_userauth_password(SSH_SESSION *session, const char *username,
    const char *password) {
  STRING *user = NULL;
  STRING *service = NULL;
  STRING *method = NULL;
  STRING *pwd = NULL;
  int rc = SSH_AUTH_ERROR;

  enter_function();

#ifdef HAVE_SSH1
  if (session->version == 1) {
    rc = ssh_userauth1_password(session, username, password);
    leave_function();
    return rc;
  }
#endif

  if (username == NULL) {
    if (session->options->username == NULL) {
      if (ssh_options_default_username(session->options) < 0) {
        leave_function();
        return rc;
      }
    }
    user = string_from_char(session->options->username);
  } else {
    user = string_from_char(username);
  }

  if (user == NULL) {
    leave_function();
    return rc;
  }

  if (ask_userauth(session) < 0) {
    string_free(user);
    leave_function();
    return rc;
  }

  service = string_from_char("ssh-connection");
  if (service == NULL) {
    goto error;
  }
  method = string_from_char("password");
  if (method == NULL) {
    goto error;
  }
  pwd = string_from_char(password);
  if (pwd == NULL) {
    goto error;
  }

  if (buffer_add_u8(session->out_buffer, SSH2_MSG_USERAUTH_REQUEST) < 0 ||
      buffer_add_ssh_string(session->out_buffer, user) < 0 ||
      buffer_add_ssh_string(session->out_buffer, service) < 0 ||
      buffer_add_ssh_string(session->out_buffer, method) < 0 ||
      buffer_add_u8(session->out_buffer, 0) < 0 ||
      buffer_add_ssh_string(session->out_buffer, pwd) < 0) {
    goto error;
  }

  string_free(user);
  string_free(service);
  string_free(method);
  string_burn(pwd);
  string_free(pwd);

  if (packet_send(session) != SSH_OK) {
    leave_function();
    return rc;
  }
  rc = wait_auth_status(session, 0);

  leave_function();
  return rc;
error:
  buffer_free(session->out_buffer);
  string_free(user);
  string_free(service);
  string_free(method);
  string_burn(pwd);
  string_free(pwd);

  leave_function();
  return rc;
}

static struct keys_struct keytab[] = {
  {
    .privatekey = "%s/.ssh/identity",
    .publickey = "%s/.ssh/identity.pub"
  },
  {
    .privatekey = "%s/.ssh/id_dsa",
    .publickey = "%s/.ssh/id_dsa.pub",
  },
  {
    .privatekey = "%s/.ssh/id_rsa",
    .publickey = "%s/.ssh/id_rsa.pub",
  },
  {
    .privatekey = NULL,
    .publickey = NULL
  }
};

/* this function initialy was in the client */
/* but the fools are the ones who never change mind */

/**
 * @brief Tries to automaticaly authenticate with public key and "none"
 *
 * It may fail, for instance it doesn't ask for a password and uses a default
 * asker for passphrases (in case the private key is encrypted).
 *
 * @param session       The ssh session to authenticate with.
 *
 * @param passphrase    Use this passphrase to unlock the privatekey. Use NULL
 *                      if you don't want to use a passphrase or the user
 *                      should be asked.
 *
 * @returns SSH_AUTH_ERROR:   A serious error happened\n
 *          SSH_AUTH_DENIED:  Authentication failed: use another method\n
 *          SSH_AUTH_PARTIAL: You've been partially authenticated, you still
 *                            have to use another method\n
 *          SSH_AUTH_SUCCESS: Authentication success
 *
 * @see ssh_userauth_kbdint()
 * @see ssh_userauth_password()
 * @see ssh_options_set_identity()
 */
int ssh_userauth_autopubkey(SSH_SESSION *session, const char *passphrase) {
  struct public_key_struct *publickey;
  STRING *pubkey;
  PRIVATE_KEY *privkey;
  char *privkeyfile = NULL;
  char *id = NULL;
  size_t size;
  unsigned int i = 0;
  int type = 0;
  int rc;

  enter_function();

  /* Always test none authentication */
  rc = ssh_userauth_none(session, NULL);
  if (rc == SSH_AUTH_ERROR || rc == SSH_AUTH_SUCCESS) {
    leave_function();
    return rc;
  }

  /* Try authentication with ssh-agent first */
#ifndef _WIN32
  if (agent_is_running(session)) {
    ssh_log(session, SSH_LOG_RARE,
        "Trying to authenticate with SSH agent keys");

    for (publickey = agent_get_first_ident(session, &privkeyfile);
        publickey != NULL;
        publickey = agent_get_next_ident(session, &privkeyfile)) {

      ssh_log(session, SSH_LOG_RARE, "Trying identity %s", privkeyfile);

      pubkey = publickey_to_string(publickey);
      if (pubkey) {
        rc = ssh_userauth_offer_pubkey(session, NULL, publickey->type, pubkey);
        string_free(pubkey);
        if (rc == SSH_AUTH_ERROR) {
          SAFE_FREE(id);
          SAFE_FREE(privkeyfile);
          publickey_free(publickey);
          leave_function();

          return rc;
        } else if (rc != SSH_AUTH_SUCCESS) {
          ssh_log(session, SSH_LOG_PACKET, "Public key refused by server\n");
          SAFE_FREE(id);
          SAFE_FREE(privkeyfile);
          publickey_free(publickey);
          continue;
        }
        ssh_log(session, SSH_LOG_RARE, "Public key accepted");
        /* pubkey accepted by server ! */
        rc = ssh_userauth_agent_pubkey(session, NULL, publickey);
        if (rc == SSH_AUTH_ERROR) {
          SAFE_FREE(id);
          SAFE_FREE(privkeyfile);
          publickey_free(publickey);
          leave_function();

          return rc;
        } else if (rc != SSH_AUTH_SUCCESS) {
          ssh_log(session, SSH_LOG_RARE,
              "Server accepted public key but refused the signature\n"
              "It might be a bug of libssh\n");
          SAFE_FREE(id);
          SAFE_FREE(privkeyfile);
          publickey_free(publickey);
          continue;
        }
        /* auth success */
        ssh_log(session, SSH_LOG_RARE, "Authentication using %s success\n",
            privkeyfile);
        SAFE_FREE(id);
        SAFE_FREE(privkeyfile);
        publickey_free(publickey);

        leave_function();

        return SSH_AUTH_SUCCESS;
      } /* if pubkey */
      SAFE_FREE(id);
      SAFE_FREE(privkeyfile);
      publickey_free(publickey);
    } /* for each privkey */
  } /* if agent is running */
#endif

  size = ARRAY_SIZE(keytab);
  if (session->options->identity) {
    ssh_log(session, SSH_LOG_RARE,
        "Trying identity file %s\n", session->options->identity);

    id = malloc(strlen(session->options->identity) + 1 + 4);
    if (id == NULL) {
      leave_function();
      return SSH_AUTH_ERROR;
    }
    sprintf(id, "%s.pub", session->options->identity);

    keytab[size - 1].privatekey = session->options->identity;
    keytab[size - 1].publickey = id;
  }

  for (i = 0, pubkey = try_publickey_from_file(session, keytab[i],
        &privkeyfile, &type);
      i < size;
      pubkey = try_publickey_from_file(session, keytab[i++],
        &privkeyfile, &type)) {
    if (pubkey == NULL) {
      continue;
    }

    rc = ssh_userauth_offer_pubkey(session, NULL, type, pubkey);
    if (rc == SSH_AUTH_ERROR){
      if (id != NULL) {
        keytab[size - 1].privatekey = NULL;
        keytab[size - 1].publickey  = NULL;
        SAFE_FREE(id);
      }
      string_free(pubkey);
      SAFE_FREE(privkeyfile);
      leave_function();
      return rc;
    } else {
      if (rc != SSH_AUTH_SUCCESS){
        ssh_log(session, SSH_LOG_RARE, "Public key refused by server");
        string_free(pubkey);
        pubkey = NULL;
        SAFE_FREE(privkeyfile);
        privkeyfile = NULL;
        continue;
      }
    }

    /* Public key accepted by server! */
    privkey = privatekey_from_file(session, privkeyfile, type, passphrase);
    if (privkey == NULL) {
      ssh_log(session, SSH_LOG_FUNCTIONS,
          "Reading private key %s failed (bad passphrase ?)",
          privkeyfile);
      string_free(pubkey);
      pubkey = NULL;
      SAFE_FREE(privkeyfile);
      privkeyfile = NULL;
      continue; /* continue the loop with other pubkey */
    }

    rc = ssh_userauth_pubkey(session, NULL, pubkey, privkey);
    if (rc == SSH_AUTH_ERROR) {
      if (id != NULL) {
        keytab[size - 1].privatekey = NULL;
        keytab[size - 1].publickey  = NULL;
        SAFE_FREE(id);
      }
      string_free(pubkey);
      SAFE_FREE(privkeyfile);
      privatekey_free(privkey);
      leave_function();
      return rc;
    } else {
      if (rc != SSH_AUTH_SUCCESS){
        ssh_log(session, SSH_LOG_FUNCTIONS,
            "The server accepted the public key but refused the signature");
        string_free(pubkey);
        pubkey = NULL;
        SAFE_FREE(privkeyfile);
        privkeyfile = NULL;
        privatekey_free(privkey);
        continue;
      }
    }

    /* auth success */
    ssh_log(session, SSH_LOG_RARE,
        "Successfully authenticated using %s", privkeyfile);
    string_free(pubkey);
    privatekey_free(privkey);
    SAFE_FREE(privkeyfile);
    if (id != NULL) {
      keytab[size - 1].privatekey = NULL;
      keytab[size - 1].publickey  = NULL;
      SAFE_FREE(id);
    }

    leave_function();
    return SSH_AUTH_SUCCESS;
  }
  /* at this point, pubkey is NULL and so is privkeyfile */
  ssh_log(session, SSH_LOG_FUNCTIONS,
      "Tried every public key, none matched");
  ssh_set_error(session,SSH_NO_ERROR,"No public key matched");
  if (id) {
    keytab[size - 1].privatekey = NULL;
    keytab[size - 1].publickey  = NULL;
    SAFE_FREE(id);
  }

  leave_function();
  return SSH_AUTH_DENIED;
}

static struct ssh_kbdint *kbdint_new(void) {
  struct ssh_kbdint *kbd;

  kbd = malloc(sizeof (struct ssh_kbdint));
  if (kbd == NULL) {
    return NULL;
  }
  ZERO_STRUCTP(kbd);

  return kbd;
}


static void kbdint_free(struct ssh_kbdint *kbd) {
  int i, n;

  if (kbd == NULL) {
    return;
  }

  n = kbd->nprompts;

  SAFE_FREE(kbd->name);
  SAFE_FREE(kbd->instruction);
  SAFE_FREE(kbd->echo);

  if (kbd->prompts) {
    for (i = 0; i < n; i++) {
      BURN_STRING(kbd->prompts[i]);
      SAFE_FREE(kbd->prompts[i]);
    }
    SAFE_FREE(kbd->prompts);
  }
  if (kbd->answers) {
    for (i = 0; i < n; i++) {
      BURN_STRING(kbd->answers[i]);
      SAFE_FREE(kbd->answers[i]);
    }
    SAFE_FREE(kbd->answers);
  }

  SAFE_FREE(kbd);
}

static void kbdint_clean(struct ssh_kbdint *kbd) {
  int i, n;

  if (kbd == NULL) {
    return;
  }

  n = kbd->nprompts;

  SAFE_FREE(kbd->name);
  SAFE_FREE(kbd->instruction);
  SAFE_FREE(kbd->echo);

  if (kbd->prompts) {
    for (i = 0; i < n; i++) {
      BURN_STRING(kbd->prompts[i]);
      SAFE_FREE(kbd->prompts[i]);
    }
    SAFE_FREE(kbd->prompts);
  }

  if (kbd->answers) {
    for (i = 0; i < n; i++) {
      BURN_STRING(kbd->answers[i]);
      SAFE_FREE(kbd->answers[i]);
    }
      SAFE_FREE(kbd->answers);
  }

  kbd->nprompts = 0;
}

/* this function sends the first packet as explained in section 3.1
 * of the draft */
static int kbdauth_init(SSH_SESSION *session, const char *user,
    const char *submethods) {
  STRING *usr = NULL;
  STRING *sub = NULL;
  STRING *service = NULL;
  STRING *method = NULL;
  int rc = SSH_AUTH_ERROR;

  enter_function();

  usr = string_from_char(user);
  if (usr == NULL) {
    goto error;
  }
  sub = (submethods ? string_from_char(submethods) : string_from_char(""));
  if (sub == NULL) {
    goto error;
  }
  service = string_from_char("ssh-connection");
  if (service == NULL) {
    goto error;
  }
  method = string_from_char("keyboard-interactive");
  if (method == NULL) {
    goto error;
  }

  if (buffer_add_u8(session->out_buffer, SSH2_MSG_USERAUTH_REQUEST) < 0 ||
      buffer_add_ssh_string(session->out_buffer, usr) < 0 ||
      buffer_add_ssh_string(session->out_buffer, service) < 0 ||
      buffer_add_ssh_string(session->out_buffer, method) < 0 ||
      buffer_add_u32(session->out_buffer, 0) < 0 ||
      buffer_add_ssh_string(session->out_buffer, sub) < 0) {
    goto error;
  }

  string_free(usr);
  string_free(service);
  string_free(method);
  string_free(sub);

  if (packet_send(session) != SSH_OK) {
    leave_function();
    return rc;
  }
  rc = wait_auth_status(session,1);

  leave_function();
  return rc;
error:
  buffer_free(session->out_buffer);
  string_free(usr);
  string_free(service);
  string_free(method);
  string_free(sub);

  leave_function();
  return rc;
}

static int kbdauth_info_get(SSH_SESSION *session) {
  STRING *name; /* name of the "asking" window showed to client */
  STRING *instruction;
  STRING *tmp;
  u32 nprompts;
  u32 i;

  enter_function();

  name = buffer_get_ssh_string(session->in_buffer);
  instruction = buffer_get_ssh_string(session->in_buffer);
  tmp = buffer_get_ssh_string(session->in_buffer);
  buffer_get_u32(session->in_buffer, &nprompts);

  if (name == NULL || instruction == NULL || tmp == NULL) {
    string_free(name);
    string_free(instruction);
    /* tmp if empty if we got here */
    ssh_set_error(session, SSH_FATAL, "Invalid USERAUTH_INFO_REQUEST msg");
    leave_function();
    return SSH_AUTH_ERROR;
  }
  string_free(tmp);

  if (session->kbdint == NULL) {
    session->kbdint = kbdint_new();
    if (session->kbdint == NULL) {
      ssh_set_error(session, SSH_FATAL, "Not enough space");
      string_free(name);
      string_free(instruction);

      leave_function();
      return SSH_AUTH_ERROR;
    }
  } else {
    kbdint_clean(session->kbdint);
  }

  session->kbdint->name = string_to_char(name);
  string_free(name);
  if (session->kbdint->name == NULL) {
    ssh_set_error(session, SSH_FATAL, "Not enough space");
    kbdint_free(session->kbdint);
    leave_function();
    return SSH_AUTH_ERROR;
  }

  session->kbdint->instruction = string_to_char(instruction);
  string_free(instruction);
  if (session->kbdint->instruction == NULL) {
    ssh_set_error(session, SSH_FATAL, "Not enough space");
    kbdint_free(session->kbdint);
    session->kbdint = NULL;
    leave_function();
    return SSH_AUTH_ERROR;
  }

  nprompts = ntohl(nprompts);
  if (nprompts > KBDINT_MAX_PROMPT) {
    ssh_set_error(session, SSH_FATAL,
        "Too much prompt asked from server: %u (0x%.4x)",
        nprompts, nprompts);
    kbdint_free(session->kbdint);
    session->kbdint = NULL;
    leave_function();
    return SSH_AUTH_ERROR;
  }

  session->kbdint->nprompts = nprompts;
  session->kbdint->prompts = malloc(nprompts * sizeof(char *));
  if (session->kbdint->prompts == NULL) {
    session->kbdint->nprompts = 0;
    ssh_set_error(session, SSH_FATAL, "No space left");
    kbdint_free(session->kbdint);
    session->kbdint = NULL;
    leave_function();
    return SSH_AUTH_ERROR;
  }
  memset(session->kbdint->prompts, 0, nprompts * sizeof(char *));

  session->kbdint->echo = malloc(nprompts);
  if (session->kbdint->echo == NULL) {
    session->kbdint->nprompts = 0;
    ssh_set_error(session, SSH_FATAL, "No space left");
    kbdint_free(session->kbdint);
    session->kbdint = NULL;
    leave_function();
    return SSH_AUTH_ERROR;
  }
  memset(session->kbdint->echo, 0, nprompts);

  for (i = 0; i < nprompts; i++) {
    tmp = buffer_get_ssh_string(session->in_buffer);
    buffer_get_u8(session->in_buffer, &session->kbdint->echo[i]);
    if (tmp == NULL) {
      ssh_set_error(session, SSH_FATAL, "Short INFO_REQUEST packet");
      kbdint_free(session->kbdint);
      session->kbdint = NULL;
      leave_function();
      return SSH_AUTH_ERROR;
    }
    session->kbdint->prompts[i] = string_to_char(tmp);
    string_free(tmp);
    if (session->kbdint->prompts[i] == NULL) {
      ssh_set_error(session, SSH_FATAL, "Not enough space");
      kbdint_free(session->kbdint);
      session->kbdint = NULL;
      leave_function();
      return SSH_AUTH_ERROR;
    }
  }

  leave_function();
  return SSH_AUTH_INFO; /* we are not auth. but we parsed the packet */
}

/* sends challenge back to the server */
static int kbdauth_send(SSH_SESSION *session) {
  STRING *answer = NULL;
  int rc = SSH_AUTH_ERROR;
  u32 i;

  enter_function();

  if (buffer_add_u8(session->out_buffer, SSH2_MSG_USERAUTH_INFO_RESPONSE) < 0 ||
      buffer_add_u32(session->out_buffer,
        htonl(session->kbdint->nprompts)) < 0) {
    goto error;
  }

  for (i = 0; i < session->kbdint->nprompts; i++) {
    if (session->kbdint->answers[i]) {
      answer = string_from_char(session->kbdint->answers[i]);
    } else {
      answer = string_from_char("");
    }
    if (answer == NULL) {
      goto error;
    }

    if (buffer_add_ssh_string(session->out_buffer, answer) < 0) {
      goto error;
    }

    string_burn(answer);
    string_free(answer);
  }

  if (packet_send(session) != SSH_OK) {
    leave_function();
    return rc;
  }
  rc = wait_auth_status(session,1);

  leave_function();
  return rc;
error:
  buffer_free(session->out_buffer);
  string_burn(answer);
  string_free(answer);

  leave_function();
  return rc;
}

/**
 * @brief Try to authenticate through the "keyboard-interactive" method.
 *
 * @param session       The ssh session to use.
 *
 * @param user          The username to authenticate. You can specify NULL if
 *                      ssh_option_set_username() has been used. You cannot try
 *                      two different logins in a row.
 *
 * @param submethods    Undocumented. Set it to NULL.
 *
 * @returns SSH_AUTH_ERROR:   A serious error happened\n
 *          SSH_AUTH_DENIED:  Authentication failed : use another method\n
 *          SSH_AUTH_PARTIAL: You've been partially authenticated, you still
 *                            have to use another method\n
 *          SSH_AUTH_SUCCESS: Authentication success\n
 *          SSH_AUTH_INFO:    The server asked some questions. Use
 *                            ssh_userauth_kbdint_getnprompts() and such.
 *
 * @see ssh_userauth_kbdint_getnprompts()
 * @see ssh_userauth_kbdint_getname()
 * @see ssh_userauth_kbdint_getinstruction()
 * @see ssh_userauth_kbdint_getprompt()
 * @see ssh_userauth_kbdint_setanswer()
 */
int ssh_userauth_kbdint(SSH_SESSION *session, const char *user,
    const char *submethods) {
  int rc = SSH_AUTH_ERROR;

  if (session->version == 1) {
    /* No keyb-interactive for ssh1 */
    return SSH_AUTH_DENIED;
  }

  enter_function();

  if (session->kbdint == NULL) {
    /* first time we call. we must ask for a challenge */
    if (user == NULL) {
      if ((user = session->options->username) == NULL) {
        if (ssh_options_default_username(session->options) < 0) {
          leave_function();
          return SSH_AUTH_ERROR;
        } else {
          user = session->options->username;
        }
      }
    }

    if (ask_userauth(session)) {
      leave_function();
      return SSH_AUTH_ERROR;
    }

    rc = kbdauth_init(session, user, submethods);
    if (rc != SSH_AUTH_INFO) {
      leave_function();
      return rc; /* error or first try success */
    }

    rc = kbdauth_info_get(session);
    if (rc == SSH_AUTH_ERROR) {
      kbdint_free(session->kbdint);
      session->kbdint = NULL;
    }

    leave_function();
    return rc;
  }

  /*
   * If we are at this point, it ss because session->kbdint exists.
   * It means the user has set some informations there we need to send
   * the server and then we need to ack the status (new questions or ok
   * pass in).
   */
  rc = kbdauth_send(session);
  kbdint_free(session->kbdint);
  session->kbdint = NULL;

  if(rc != SSH_AUTH_INFO) {
    leave_function();
    return rc;
  }

  rc = kbdauth_info_get(session);
  if (rc == SSH_AUTH_ERROR) {
    kbdint_free(session->kbdint);
    session->kbdint = NULL;
  }

  leave_function();
  return rc;
}

/**
 * @brief Get the number of prompts (questions) the server has given.
 *
 * You have called ssh_userauth_kbdint() and got SSH_AUTH_INFO. This
 * function returns the questions from the server.
 *
 * @param session       The ssh session to use.
 *
 * @returns             The number of prompts.
 */
int ssh_userauth_kbdint_getnprompts(SSH_SESSION *session) {
  return session->kbdint->nprompts;
}

/**
 * @brief Get the "name" of the message block.
 *
 * You have called ssh_userauth_kbdint() and got SSH_AUTH_INFO. This
 * function returns the questions from the server.
 *
 * @param session       The ssh session to use.
 *
 * @returns             The name of the message block. Do not free it.
 */
char *ssh_userauth_kbdint_getname(SSH_SESSION *session) {
  return session->kbdint->name;
}

/**
 * @brief Get the "instruction" of the message block.
 *
 * You have called ssh_userauth_kbdint() and got SSH_AUTH_INFO. This
 * function returns the questions from the server.
 *
 * @param session       The ssh session to use.
 *
 * @returns             The instruction of the message block.
 */

char *ssh_userauth_kbdint_getinstruction(SSH_SESSION *session) {
  return session->kbdint->instruction;
}

/**
 * @brief Get a prompt from a message block.
 *
 * You have called ssh_userauth_kbdint() and got SSH_AUTH_INFO. This
 * function returns the questions from the server.
 *
 * @param session       The ssh session to use.
 *
 * @param i             The inndex number of the i'th prompt.
 *
 * @param echo          When different of NULL, it will obtain a boolean meaning
 *                      that the resulting user input should be echoed or not
 *                      (like passwords).
 *
 * @returns             A pointer to the prompt. Do not free it.
 */
char *ssh_userauth_kbdint_getprompt(SSH_SESSION *session, unsigned int i,
    char *echo) {
  if (i > session->kbdint->nprompts) {
    return NULL;
  }

  if (echo) {
    *echo = session->kbdint->echo[i];
  }

  return session->kbdint->prompts[i];
}

/** You have called ssh_userauth_kbdint() and got SSH_AUTH_INFO. this
 * function returns the questions from the server
 * \brief set the answer for a question from a message block.
 * \param session ssh session
 * \param i index number of the ith prompt
 * \param answer answer to give to server
 * \return 0 on success, < 0 on error.
 */
int ssh_userauth_kbdint_setanswer(SSH_SESSION *session, unsigned int i,
    const char *answer) {
  if (i > session->kbdint->nprompts) {
    return -1;
  }

  if (session->kbdint->answers == NULL) {
    session->kbdint->answers = malloc(sizeof(char*) * session->kbdint->nprompts);
    if (session->kbdint->answers == NULL) {
      return -1;
    }
    memset(session->kbdint->answers, 0, sizeof(char *) * session->kbdint->nprompts);
  }

  if (session->kbdint->answers[i]) {
    BURN_STRING(session->kbdint->answers[i]);
    SAFE_FREE(session->kbdint->answers[i]);
  }

  session->kbdint->answers[i] = strdup(answer);
  if (session->kbdint->answers[i] == NULL) {
    return -1;
  }

  return 0;
}

/** @} */
/* vim: set ts=2 sw=2 et cindent: */
