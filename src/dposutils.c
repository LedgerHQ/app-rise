#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include "os.h"
#include "cx.h"
#include "dposutils.h"


/**
 * Gets a bigendian representation of the usable publicKey
 * @param publicKey the raw public key containing both coordinated for the elliptic curve
 * @param encoded result holder
 */
void getEncodedPublicKey(cx_ecfp_public_key_t *publicKey, uint8_t *encoded) {
  uint8_t i;
  for (i = 0; i < 32; i++) {
    encoded[i] = publicKey->W[64 - i];
  }
  if ((publicKey->W[32] & 1) != 0) {
    encoded[31] |= 0x80;
  }
}


/**
 * Derive address uint64_t value from a byte array.
 * @param source source byte array where the next 8 bytes represent the address
 * @param rev whether or not to reverse the info. (publickey address derivation => true)
 * @return the encoded address in uint64_t data.
 */
uint64_t deriveAddressFromUintArray(uint8_t *source, bool rev) {

  uint8_t address[8];
  uint8_t i;
  for (i = 0; i < 8; i++) {
    address[i] = source[rev == true ? 7 - i : i];
  }

  uint64_t encodedAddress = 0;
  for (i = 0; i < 8; i++) {
    encodedAddress = encodedAddress << 8;
    encodedAddress += address[i];
  }

  return encodedAddress;
}


/**
 * Returns a string representation of the encoded Address
 * @param encodedAddress address to represent
 * @param output the output where the string representation will be returned
 * @return the length of the string representation.
 */
uint8_t deriveAddressStringRepresentation(uint64_t encodedAddress, char *output) {

  char brocca[22];
  uint8_t i = 0;
  while (encodedAddress > 0) {
    uint64_t remainder = encodedAddress % 10;
    encodedAddress = encodedAddress / 10;
    brocca[i++] = (char) (remainder + '0');
  }

  uint8_t total = i;
  for (i = 0; i < total; i++) {
    output[total - 1 - i] = brocca[i];
  }

  os_memmove(&output[total], ADDRESS_SUFFIX, ADDRESS_SUFFIX_LENGTH);
  output[total + ADDRESS_SUFFIX_LENGTH] = '\0'; // for strlen
  return (uint8_t) (total + ADDRESS_SUFFIX_LENGTH /*suffix*/);
}

//uint8_t deriveAddressShortRepresentation(uint64_t encodedAddress, char *output) {
//  char tmp[14];
//  deriveAddressStringRepresentation(encodedAddress, output);
//  size_t length = strlen(output);
//
//
//  os_memmove(tmp, output, 5);
//  os_memmove(tmp + 5, "...", 3);
//  os_memmove(tmp + 5 + 3, output + length - 5, 5);
//  tmp[13] = '\0';
//
//  os_memmove(output, tmp, 14);
//  return 13;
//}
const char hexChars[] = "0123456789abcdef";
/**
 * Derive address associated to the specific publicKey.
 * @param publicKey original publicKey
 * @return the encoded address.
 */
uint64_t deriveAddressFromPublic(cx_ecfp_public_key_t *publicKey) {
  uint8_t encodedPkey[32];

  getEncodedPublicKey(publicKey, encodedPkey);

  unsigned char hashedPkey[32];
  cx_hash_sha256(encodedPkey, 32, hashedPkey);

  return deriveAddressFromUintArray(
    hashedPkey,
    true
  );
}

void toHex(uint8_t what, char * whereTo) {
  whereTo[0] = hexChars[what / 16];
  whereTo[1] = hexChars[what % 16];
}


void parseTransaction(uint8_t *txBytes, uint32_t txLength, bool hasRequesterPublicKey, struct transaction *out) {
  out->type = txBytes[0];
  uint32_t recIndex = 1 /*type*/
                      + 4 /*timestamp*/
                      + 32 /*senderPublicKey */
                      + (hasRequesterPublicKey == true ? 32 : 0) /*requesterPublicKey */;
  out->recipientId = deriveAddressFromUintArray(txBytes + recIndex, false);
  uint32_t i = 0;
  out->amountSatoshi = 0;
  for (i = 0; i < 8; i++) {
    out->amountSatoshi |= ((uint64_t )txBytes[recIndex + 8 + i]) << (8*i);
  }

  os_memset(out->shortDesc, 0, 22);
  if (out->type == TXTYPE_CREATESIGNATURE) {
    // Read publickey from bytes.
    // it's 32 bytes.
    for (i=0; i < 3; i++) {
      toHex(txBytes[recIndex + 8 + 8 + i], out->shortDesc + i*2);
    }
    out->shortDesc[5] = '.';
    out->shortDesc[6] = '.';
    for (i=0; i < 3; i++) {
      toHex(txBytes[recIndex + 8 + 8 + 32 - 3 + i], out->shortDesc + 7 + i*2);
    }
    out->shortDesc[7] = '.';
  } else if (out->type == TXTYPE_REGISTERDELEGATE) {
    os_memmove(out->shortDesc, txBytes+recIndex + 8 + 8, MIN(21, txLength - (recIndex + 8 + 8)));
    for (i=0; i<MIN(21, txLength - (recIndex + 8 + 8)); i++) {
      char c = out->shortDesc[i];
      if (
        !(c >= 'a' && c <= 'z') &&
        !(c >= '0' && c <= '9') &&
        !(c == '!' || c == '@' || c == '$' || c == '&' || c == '_' || c == '.')) {
        out->shortDesc[i] = '\0';
      }
    }
  } else if (out->type == TXTYPE_VOTE) {
    uint8_t added = 0;
    uint8_t removed = 0;
    for (i=recIndex + 8 + 8; i<txLength; i++) {
      if (txBytes[i] == '+') {
        added++;
      } else if (txBytes[i] == '-') {
        removed++;
      }
    }
    // Storing the amount of added and removed votes in shortDesc.
    // Will later be used in the ui_elements thing.
    out->shortDesc[0] = added;
    out->shortDesc[1] = removed;
  } else if (out->type == TXTYPE_CREATEMULTISIG) {
    out->shortDesc[0] = txBytes[recIndex+8+8];
    out->shortDesc[1] = txBytes[recIndex+8+8+1];
  }
}
