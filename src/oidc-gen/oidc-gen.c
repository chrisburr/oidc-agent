#include "oidc-gen.h"

#include "gen_handler.h"
// #include "privileges/gen_privileges.h"
#include "utils/accountUtils.h"
#include "utils/commonFeatures.h"
#include "utils/disableTracing.h"
#include "utils/file_io/fileUtils.h"

#include <stdio.h>
#include <stdlib.h>
#include "utils/logger.h"

int main(int argc, char** argv) {
  platform_disable_tracing();
  logger_open("oidc-gen");
  setlogmask(LOG_UPTO(LOG_NOTICE));

  struct arguments arguments;
  initArguments(&arguments);
  argp_parse(&argp, argc, argv, 0, 0, &arguments);
  if (arguments.debug) {
    setlogmask(LOG_UPTO(LOG_DEBUG));
  }
  // if (arguments.seccomp) {
  //   initOidcGenPrivileges(&arguments);
  // }

  assertOidcDirExists();

  if (arguments.listClients) {
    gen_handleList();
  }
  if (arguments.listAccounts) {
    common_handleListAccountConfigs();
  }
  if (arguments.listClients || arguments.listAccounts) {
    exit(EXIT_SUCCESS);
  }
  if (arguments.print) {
    gen_handlePrint(arguments.print, &arguments);
    exit(EXIT_SUCCESS);
  }
  if (arguments.updateConfigFile) {
    gen_handleUpdateConfigFile(arguments.updateConfigFile, &arguments);
    exit(EXIT_SUCCESS);
  }
  if (arguments.codeExchange) {
    handleCodeExchange(&arguments);
    exit(EXIT_SUCCESS);
  }
  common_assertAgent();

  if (arguments.state) {
    stateLookUpWithConfigSave(arguments.state, &arguments);
    exit(EXIT_SUCCESS);
  }

  if (arguments.delete) {
    handleDelete(&arguments);
    exit(EXIT_SUCCESS);
  }

  if (arguments.reauthenticate) {
    reauthenticate(arguments.args[0], &arguments);
    exit(EXIT_SUCCESS);
  }

  struct oidc_account* account = NULL;
  if (arguments.manual) {
    if (arguments.file) {
      account = getAccountFromMaybeEncryptedFile(arguments.file);
    }
    manualGen(account, &arguments);
  } else {
    struct oidc_account* account = registerClient(&arguments);
    if (account) {
      handleGen(account, &arguments, NULL);
    } else {
      list_destroy(arguments.flows);
      exit(EXIT_FAILURE);
    }
  }
  list_destroy(arguments.flows);
  exit(EXIT_SUCCESS);
}
