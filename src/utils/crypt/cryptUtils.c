#include "cryptUtils.h"
#include "account/account.h"
#include "crypt.h"
#include "defines/settings.h"
#include "defines/version.h"
#include "hexCrypt.h"
#include "list/list.h"
#include "memoryCrypt.h"
#include "utils/db/account_db.h"
#include "utils/file_io/file_io.h"
#include "utils/file_io/oidc_file_io.h"
#include "utils/memory.h"
#include "utils/oidc_error.h"
#include "utils/prompt.h"
#include "utils/promptUtils.h"
#include "utils/versionUtils.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

/**
 * @brief encrypts and writes a given text.
 * @param text the json encoded account configuration text
 * @param suggestedPassword the suggestedPassword for encryption, won't be
 * displayed; can be NULL.
 * @param filepath an absolute path to the output file. Either filepath or
 * filename has to be given. The other one shall be NULL.
 * @param filename the filename of the output file. The output file will be
 * placed in the oidc dir. Either filepath or filename has to be given. The
 * other one shall be NULL.
 * @return an oidc_error code. oidc_errno is set properly.
 */
oidc_error_t promptEncryptAndWriteText(const char* text, const char* hint,
                                       const char* suggestedPassword,
                                       const char* filepath,
                                       const char* oidc_filename) {
  initCrypt();
  char* encryptionPassword =
      getEncryptionPasswordFor(hint, suggestedPassword, NULL /*TODO*/);
  if (encryptionPassword == NULL) {
    return oidc_errno;
  }
  oidc_error_t ret = encryptAndWriteUsingPassword(text, encryptionPassword,
                                                  filepath, oidc_filename);
  secFree(encryptionPassword);
  return ret;
}

/**
 * @brief decrypts a file in the oidcdir with the given password
 * @param filename the filename of the oidc-file
 * @param password if not @c NULL @p password is used for decryption; if @c NULL
 * the user will be prompted
 * @return a pointer to the decrypted filecontent. It has to be freed after
 * usage.
 */
char* decryptOidcFile(const char* filename, const char* password) {
  char* filepath = concatToOidcDir(filename);
  char* ret      = decryptFile(filepath, password);
  secFree(filepath);
  return ret;
}

/**
 * @brief decrypts a file with the given password
 * @param filepath the path to the file to be decrypted
 * @param password if not @c NULL @p password is used for decryption; if @c NULL
 * the user will be prompted
 * @return a pointer to the decrypted filecontent. It has to be freed after
 * usage.
 */
char* decryptFile(const char* filepath, const char* password) {
  list_t* lines = getLinesFromFile(filepath);
  if (lines == NULL) {
    return NULL;
  }
  char* promptedPassword = NULL;
  char* ret              = NULL;
  if (password) {
    ret = decryptLinesList(lines, password);
  } else {
    for (size_t i = 0; i < MAX_PASS_TRIES && ret == NULL; i++) {
      promptedPassword = promptPassword("Enter decryption Password: ");
      ret              = decryptLinesList(lines, promptedPassword);
      secFree(promptedPassword);
      if (ret == NULL) {
        oidc_perror();
      }
    }
  }
  list_destroy(lines);
  return ret;
}

/**
 * @brief decrypts the content of a file with the given password.
 * the file content must be content generated by @c encryptWithVersionLine
 * @param filecontent the filecontent to be decrypted
 * @param password the password used for encryption
 * @return a pointer to the decrypted filecontent. It has to be freed after
 * usage.
 */
char* decryptFileContent(const char* fileContent, const char* password) {
  list_t* lines = delimitedStringToList(fileContent, '\n');
  char*   ret   = decryptLinesList(lines, password);
  list_destroy(lines);
  return ret;
}

/**
 * @brief decrypts the content of a hex encoded file with the given password.
 * the file must have been generated before version 2.1.0
 * @param cipher the filecontent to be decrypted
 * @param password the password used for encryption
 * @return a pointer to the decrypted filecontent. It has to be freed after
 * usage.
 */
char* decryptHexFileContent(const char* cipher, const char* password) {
  char*         fileText       = oidc_strcopy(cipher);
  unsigned long cipher_len     = strToInt(strtok(fileText, ":"));
  char*         salt_encoded   = strtok(NULL, ":");
  char*         nonce_encoded  = strtok(NULL, ":");
  char*         cipher_encoded = strtok(NULL, ":");
  if (cipher_len == 0 || salt_encoded == NULL || nonce_encoded == NULL ||
      cipher_encoded == NULL) {
    oidc_errno = OIDC_ECRYPM;
    secFree(fileText);
    return NULL;
  }
  unsigned char* decrypted = crypt_decrypt_hex(
      cipher_encoded, cipher_len, password, nonce_encoded, salt_encoded);
  secFree(fileText);
  return (char*)decrypted;
}

/**
 * @brief decrypts a list of lines with the given password.
 * The list has to contain sepcific information in the correct order; the last
 * line has to be the version line (if there is one, files encrypted before
 * 2.1.0 will only have on line).
 * @param lines the list of lines
 * @param password the password used for encryption
 * @return a pointer to the decrypted cipher. It has to be freed after
 * usage.
 */
char* decryptLinesList(list_t* lines, const char* password) {
  if (lines == NULL) {
    oidc_setArgNullFuncError(__func__);
    return NULL;
  }
  list_node_t* node   = list_at(lines, 0);
  char*        cipher = node ? node->val : NULL;
  node                = list_at(lines, -1);
  char* version_line  = lines->len > 1 ? node ? node->val : NULL : NULL;
  char* version       = versionLineToSimpleVersion(version_line);
  if (versionAtLeast(version, MIN_BASE64_VERSION)) {
    secFree(version);
    return crypt_decryptFromList(lines, password);
  } else {  // old config file format; using hex encoding
    secFree(version);
    return decryptHexFileContent(cipher, password);
  }
}

/**
 * @brief decrypts a cipher that was generated with a specific version with the
 * given password
 * @param cipher the cipher to be decrypted - this is a formatted string
 * containing all relevant encryption information; the format differ with the
 * used version
 * @param password the password used for encryption
 * @param version the oidc-agent version that was used when cipher was encrypted
 * @return a pointer to the decrypted cipher. It has to be freed after
 * usage.
 */
char* decryptText(const char* cipher, const char* password,
                  const char* version) {
  if (cipher == NULL || password == NULL) {  // allow NULL for version
    oidc_setArgNullFuncError(__func__);
    return NULL;
  }
  if (versionAtLeast(version, MIN_BASE64_VERSION)) {
    return crypt_decrypt(cipher, password);
  } else {  // old config file format; using hex encoding
    return decryptHexFileContent(cipher, password);
  }
}

/**
 * @brief encrypts a given text with the given password
 * @return the encrypted text in a formatted string that holds all relevant
 * encryption information and that can be passed to @c decryptText
 * @note when saving the encrypted text you also have to save the oidc-agent
 * version. But there is a specific function for this. See
 * @c encryptWithVersionLine
 * @note before version 2.1.0 this function used hex encoding
 */
char* encryptText(const char* text, const char* password) {
  return crypt_encrypt(text, password);
}

/**
 * @brief encrypts a given text with the given password and adds the current
 * oidc-agent version
 * @return the encrypted text in a formatted string that holds all relevant
 * encryption information as well as the oidc-agent version. Can be passed to
 * @c decryptFileContent
 */
char* encryptWithVersionLine(const char* text, const char* password) {
  char* crypt        = encryptText(text, password);
  char* version_line = simpleVersionToVersionLine(VERSION);
  char* ret          = oidc_sprintf("%s\n%s", crypt, version_line);
  secFree(crypt);
  secFree(version_line);
  return ret;
}

char* encryptForIpc(const char* msg, const unsigned char* key) {
  struct encryptionInfo* cryptResult =
      crypt_encryptWithKey((unsigned char*)msg, key);
  if (cryptResult->encrypted_base64 == NULL) {
    secFreeEncryptionInfo(cryptResult);
    return NULL;
  }
  char* encoded =
      oidc_sprintf("%lu:%s:%s", strlen(msg), cryptResult->nonce_base64,
                   cryptResult->encrypted_base64);
  secFreeEncryptionInfo(cryptResult);
  return encoded;
}

/**
 * @brief encrypts and writes a given text with the given password.
 * @param text the text to be encrypted
 * @param password the encryption password
 * @param filepath an absolute path to the output file. Either filepath or
 * filename has to be given. The other one shall be NULL.
 * @param filename the filename of the output file. The output file will be
 * placed in the oidc dir. Either filepath or filename has to be given. The
 * other one shall be NULL.
 * @return an oidc_error code. oidc_errno is set properly.
 */
oidc_error_t encryptAndWriteUsingPassword(const char* text,
                                          const char* password,
                                          const char* filepath,
                                          const char* oidc_filename) {
  if (text == NULL || password == NULL ||
      (filepath == NULL && oidc_filename == NULL)) {
    oidc_setArgNullFuncError(__func__);
    return oidc_errno;
  }
  char* toWrite = encryptWithVersionLine(text, password);
  if (toWrite == NULL) {
    return oidc_errno;
  }
  if (filepath) {
    syslog(LOG_AUTHPRIV | LOG_DEBUG, "Write to file %s", filepath);
    writeFile(filepath, toWrite);
  } else {
    syslog(LOG_AUTHPRIV | LOG_DEBUG, "Write to oidc file %s", oidc_filename);
    writeOidcFile(oidc_filename, toWrite);
  }
  secFree(toWrite);
  return OIDC_SUCCESS;
}

char* decryptForIpc(const char* msg, const unsigned char* key) {
  if (msg == NULL) {
    oidc_setArgNullFuncError(__func__);
    return NULL;
  }
  char*  msg_tmp          = oidc_strcopy(msg);
  char*  len_str          = strtok(msg_tmp, ":");
  char*  nonce_base64     = strtok(NULL, ":");
  char*  encrypted_base64 = strtok(NULL, ":");
  size_t msg_len          = strToULong(len_str);
  if (nonce_base64 == NULL || encrypted_base64 == NULL) {
    secFree(msg_tmp);
    oidc_errno = OIDC_ECRYPMIPC;
    return NULL;
  }
  struct encryptionInfo crypt = {};
  crypt.nonce_base64          = nonce_base64;
  crypt.encrypted_base64      = encrypted_base64;
  crypt.cryptParameter        = newCryptParameters();
  unsigned char* decryptedMsg =
      crypt_decryptWithKey(&crypt, msg_len + crypt.cryptParameter.mac_len, key);
  secFree(msg_tmp);
  return (char*)decryptedMsg;
}

/**
 * @brief encrypts sensitive information when the agent is locked.
 * encrypts all loaded access_token, additional encryption (on top of already in
 * place xor) for refresh_token, client_id, client_secret
 * @param loaded the list of currently loaded accounts
 * @param password the lock password that will be used for encryption
 * @return an oidc_error code
 */
oidc_error_t lockEncrypt(const char* password) {
  list_node_t*     node;
  list_iterator_t* it = list_iterator_new(accountDB_getList(), LIST_HEAD);
  while ((node = list_iterator_next(it))) {
    struct oidc_account* acc = node->val;
    char* tmp = encryptText(account_getAccessToken(acc), password);
    if (tmp == NULL) {
      return oidc_errno;
    }
    account_setAccessToken(acc, tmp);
    tmp = encryptText(account_getRefreshToken(acc), password);
    if (tmp == NULL) {
      return oidc_errno;
    }
    account_setRefreshToken(acc, tmp);
    tmp = encryptText(account_getClientId(acc), password);
    if (tmp == NULL) {
      return oidc_errno;
    }
    account_setClientId(acc, tmp);
    if (strValid(account_getClientSecret(acc))) {
      tmp = encryptText(account_getClientSecret(acc), password);
      if (tmp == NULL) {
        return oidc_errno;
      }
      account_setClientSecret(acc, tmp);
    }
  }
  list_iterator_destroy(it);
  return OIDC_SUCCESS;
}

/**
 * @brief decrypts sensitive information when the agent is unlocked.
 * After this call refresh_token, client_id, and client_secret will still be
 * xor encrypted
 * @param loaded the list of currently loaded accounts
 * @param password the lock password that was used for encryption
 * @return an oidc_error code
 */
oidc_error_t lockDecrypt(const char* password) {
  list_node_t*     node;
  list_iterator_t* it = list_iterator_new(accountDB_getList(), LIST_HEAD);
  while ((node = list_iterator_next(it))) {
    struct oidc_account* acc = node->val;
    char* tmp = crypt_decrypt(account_getAccessToken(acc), password);
    if (tmp == NULL) {
      return oidc_errno;
    }
    account_setAccessToken(acc, tmp);
    tmp = crypt_decrypt(account_getRefreshToken(acc), password);
    if (tmp == NULL) {
      return oidc_errno;
    }
    account_setRefreshToken(acc, tmp);
    tmp = crypt_decrypt(account_getClientId(acc), password);
    if (tmp == NULL) {
      return oidc_errno;
    }
    account_setClientId(acc, tmp);
    tmp = crypt_decrypt(account_getClientSecret(acc), password);
    if (tmp == NULL) {
      return oidc_errno;
    }
    account_setClientSecret(acc, tmp);
  }
  list_iterator_destroy(it);
  return OIDC_SUCCESS;
}

/**
 * @brief finds an account in the list of currently loaded accounts and
 * decryptes the sensitive information
 * @param loaded_accounts the list of currently loaded accounts
 * @param key a key account that should be searched for
 * @return a pointer to the decrypted account
 * @note after usage the account has to be encrypted again by using
 * @c addAccountToList
 */
struct oidc_account* db_getAccountDecrypted(struct oidc_account* key) {
  syslog(LOG_AUTHPRIV | LOG_DEBUG, "Getting / Decrypting account from list");
  struct oidc_account* account = accountDB_findValue(key);
  if (account == NULL) {
    return NULL;
  }
  account_setRefreshToken(account,
                          memoryDecrypt(account_getRefreshToken(account)));
  account_setClientId(account, memoryDecrypt(account_getClientId(account)));
  account_setClientSecret(account,
                          memoryDecrypt(account_getClientSecret(account)));
  return account;
}

/**
 * @brief encrypts the sensitive information of an account and adds it to the
 * list of currently loaded accounts.
 * If there is already a similar account loaded it will be overwritten (removed
 * and the then the new account is added)
 * @param loaded_accounts the list of currently loaded accounts
 * @param account the account that should be added
 */
void db_addAccountEncrypted(struct oidc_account* account) {
  syslog(LOG_AUTHPRIV | LOG_DEBUG, "Adding / Reencrypting account to list");
  account_setRefreshToken(account,
                          memoryEncrypt(account_getRefreshToken(account)));
  account_setClientId(account, memoryEncrypt(account_getClientId(account)));
  account_setClientSecret(account,
                          memoryEncrypt(account_getClientSecret(account)));
  struct oidc_account* found = accountDB_findValue(account);
  if (found && found != account) {
    accountDB_removeIfFound(account);
  }
  if (found != account) {
    accountDB_addValue(account);
  }
}
