/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree.
 */

#include <fizz/record/Types.h>
#include <folly/String.h>

namespace fizz {

constexpr Random HelloRetryRequest::HrrRandom;

ProtocolVersion getRealDraftVersion(ProtocolVersion version) {
  switch (version) {
    case ProtocolVersion::tls_1_3:
      return ProtocolVersion::tls_1_3;
    case ProtocolVersion::tls_1_3_20:
    case ProtocolVersion::tls_1_3_20_fb:
      return ProtocolVersion::tls_1_3_20;
    case ProtocolVersion::tls_1_3_21:
    case ProtocolVersion::tls_1_3_21_fb:
      return ProtocolVersion::tls_1_3_21;
    case ProtocolVersion::tls_1_3_22:
    case ProtocolVersion::tls_1_3_22_fb:
      return ProtocolVersion::tls_1_3_22;
    case ProtocolVersion::tls_1_3_23:
    case ProtocolVersion::tls_1_3_23_fb:
      return ProtocolVersion::tls_1_3_23;
    case ProtocolVersion::tls_1_3_26:
    case ProtocolVersion::tls_1_3_26_fb:
      return ProtocolVersion::tls_1_3_26;
    case ProtocolVersion::tls_1_3_28:
      return ProtocolVersion::tls_1_3_28;
    default:
      throw std::runtime_error(folly::to<std::string>(
          "getRealDraftVersion() called with ", toString(version)));
  }
}

std::string toString(ProtocolVersion version) {
  switch (version) {
    case ProtocolVersion::tls_1_0:
      return "TLSv1.0";
    case ProtocolVersion::tls_1_1:
      return "TLSv1.1";
    case ProtocolVersion::tls_1_2:
      return "TLSv1.2";
    case ProtocolVersion::tls_1_3:
      return "TLSv1.3";
    case ProtocolVersion::tls_1_3_20:
      return "TLSv1.3-draft-20";
    case ProtocolVersion::tls_1_3_20_fb:
      return "TLSv1.3-draft-20-fb";
    case ProtocolVersion::tls_1_3_21:
      return "TLSv1.3-draft-21";
    case ProtocolVersion::tls_1_3_21_fb:
      return "TLSv1.3-draft-21-fb";
    case ProtocolVersion::tls_1_3_22:
      return "TLSv1.3-draft-22";
    case ProtocolVersion::tls_1_3_22_fb:
      return "TLSv1.3-draft-22-fb";
    case ProtocolVersion::tls_1_3_23:
      return "TLSv1.3-draft-23";
    case ProtocolVersion::tls_1_3_23_fb:
      return "TLSv1.3-draft-23-fb";
    case ProtocolVersion::tls_1_3_26:
      return "TLSv1.3-draft-26";
    case ProtocolVersion::tls_1_3_26_fb:
      return "TLSv1.3-draft-26-fb";
    case ProtocolVersion::tls_1_3_28:
      return "TLSv1.3-draft-28";
  }
  return enumToHex(version);
}

std::string toString(ExtensionType extType) {
  switch (extType) {
    case ExtensionType::server_name:
      return "server_name";
    case ExtensionType::supported_groups:
      return "supported_groups";
    case ExtensionType::signature_algorithms:
      return "signature_algorithms";
    case ExtensionType::application_layer_protocol_negotiation:
      return "application_layer_protocol_negotiation";
    case ExtensionType::token_binding:
      return "token_binding";
    case ExtensionType::quic_transport_parameters:
      return "quic_transport_parameters";
    case ExtensionType::key_share_old:
      return "key_share_old";
    case ExtensionType::pre_shared_key:
      return "pre_shared_key";
    case ExtensionType::early_data:
      return "early_data";
    case ExtensionType::supported_versions:
      return "supported_version";
    case ExtensionType::cookie:
      return "cookie";
    case ExtensionType::psk_key_exchange_modes:
      return "psk_key_exchange_modes";
    case ExtensionType::certificate_authorities:
      return "certificate_authorities";
    case ExtensionType::post_handshake_auth:
      return "post_handshake_auth";
    case ExtensionType::signature_algorithms_cert:
      return "signature_algorithms_cert";
    case ExtensionType::key_share:
      return "key_share";
    case ExtensionType::alternate_server_name:
      return "alternate_server_name";
  }
  return enumToHex(extType);
}

std::string toString(AlertDescription alertDesc) {
  switch (alertDesc) {
    case AlertDescription::close_notify:
      return "close_notify";
    case AlertDescription::end_of_early_data:
      return "end_of_early_data";
    case AlertDescription::unexpected_message:
      return "unexpected_message";
    case AlertDescription::bad_record_mac:
      return "bad_record_mac";
    case AlertDescription::record_overflow:
      return "record_overflow";
    case AlertDescription::handshake_failure:
      return "handshake_failure";
    case AlertDescription::bad_certificate:
      return "bad_certificate";
    case AlertDescription::unsupported_certificate:
      return "unsupported_certificate";
    case AlertDescription::certificate_revoked:
      return "certificate_revoked";
    case AlertDescription::certificate_expired:
      return "certificate_expired";
    case AlertDescription::certificate_unknown:
      return "certificate_unknown";
    case AlertDescription::illegal_parameter:
      return "illegal_parameter";
    case AlertDescription::unknown_ca:
      return "unknown_ca";
    case AlertDescription::access_denied:
      return "access_denied";
    case AlertDescription::decode_error:
      return "decode_error";
    case AlertDescription::decrypt_error:
      return "decrypt_error";
    case AlertDescription::protocol_version:
      return "protocol_version";
    case AlertDescription::insufficient_security:
      return "insufficient_security";
    case AlertDescription::internal_error:
      return "internal_error";
    case AlertDescription::inappropriate_fallback:
      return "inappropriate_fallback";
    case AlertDescription::user_canceled:
      return "user_canceled";
    case AlertDescription::missing_extension:
      return "missing_extension";
    case AlertDescription::unsupported_extension:
      return "unsupported_extension";
    case AlertDescription::certificate_unobtainable:
      return "certificate_unobtainable";
    case AlertDescription::unrecognized_name:
      return "unrecognized_name";
    case AlertDescription::bad_certificate_status_response:
      return "bad_certificate_status_response";
    case AlertDescription::bad_certificate_hash_value:
      return "bad_certificate_hash_value";
    case AlertDescription::unknown_psk_identity:
      return "unknown_psk_identity";
    case AlertDescription::certificate_required:
      return "certificate_required";
  }
  return enumToHex(alertDesc);
}

std::string toString(CipherSuite cipher) {
  switch (cipher) {
    case CipherSuite::TLS_AES_128_GCM_SHA256:
      return "TLS_AES_128_GCM_SHA256";
    case CipherSuite::TLS_AES_256_GCM_SHA384:
      return "TLS_AES_256_GCM_SHA384";
    case CipherSuite::TLS_CHACHA20_POLY1305_SHA256:
      return "TLS_CHACHA20_POLY1305_SHA256";
  }
  return enumToHex(cipher);
}

std::string toString(PskKeyExchangeMode pskKeMode) {
  switch (pskKeMode) {
    case PskKeyExchangeMode::psk_ke:
      return "psk_ke";
    case PskKeyExchangeMode::psk_dhe_ke:
      return "psk_dhe_ke";
  }
  return enumToHex(pskKeMode);
}

std::string toString(SignatureScheme sigScheme) {
  switch (sigScheme) {
    case SignatureScheme::ecdsa_secp256r1_sha256:
      return "ecdsa_secp256r1_sha256";
    case SignatureScheme::ecdsa_secp384r1_sha384:
      return "ecdsa_secp384r1_sha384";
    case SignatureScheme::ecdsa_secp521r1_sha512:
      return "ecdsa_secp521r1_sha512";
    case SignatureScheme::rsa_pss_sha256:
      return "rsa_pss_sha256";
    case SignatureScheme::rsa_pss_sha384:
      return "rsa_pss_sha384";
    case SignatureScheme::rsa_pss_sha512:
      return "rsa_pss_sha512";
    case SignatureScheme::ed25519:
      return "ed25519";
    case SignatureScheme::ed448:
      return "ed448";
  }
  return enumToHex(sigScheme);
}

std::string toString(NamedGroup group) {
  switch (group) {
    case NamedGroup::secp256r1:
      return "secp256r1";
    case NamedGroup::x25519:
      return "x25519";
  }
  return enumToHex(group);
}
} // namespace fizz