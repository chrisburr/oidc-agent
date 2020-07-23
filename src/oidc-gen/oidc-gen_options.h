#ifndef OIDC_GEN_OPTIONS_H
#define OIDC_GEN_OPTIONS_H

#include "list/list.h"

#include <argp.h>

#define OPT_LONG_CLIENTID "client-id"
#define OPT_LONG_CLIENTSECRET "client-secret"
#define OPT_LONG_REFRESHTOKEN "rt"
#define OPT_LONG_USERNAME "op-username"
#define OPT_LONG_PASSWORD "op-password"
#define OPT_LONG_CERTPATH "cert-path"
#define OPT_LONG_ISSUER "issuer"
#define OPT_LONG_AUDIENCE "aud"
#define OPT_LONG_SCOPE "scope"
#define OPT_LONG_REDIRECT "redirect-uri"

struct optional_arg {
  char* str;
  short useIt;
};

struct arguments {
  char* args[1]; /* account */
  char* print;
  char* rename;
  char* updateConfigFile;
  char* codeExchange;
  char* state;
  char* device_authorization_endpoint;
  char* pw_cmd;
  char* pw_file;
  char* file;

  char* client_id;
  char* client_secret;
  char* issuer;
  char* redirect_uri;
  char* scope;
  char* dynRegToken;
  char* cert_path;
  char* refresh_token;
  char* cnid;
  char* audience;
  char* op_username;
  char* op_password;

  list_t* flows;
  list_t* redirect_uris;

  unsigned char delete;
  unsigned char listAccounts;
  unsigned char reauthenticate;
  unsigned char manual;
  unsigned char usePublicClient;
  unsigned char seccomp;
  unsigned char _nosec;
  unsigned char noUrlCall;
  unsigned char noWebserver;
  unsigned char noScheme;
  unsigned char pw_prompt_mode;
  unsigned char prompt_mode;
  unsigned char debug;
  unsigned char verbose;
};

void initArguments(struct arguments* arguments);

struct argp argp;

#endif  // OIDC_GEN_OPTIONS_H
