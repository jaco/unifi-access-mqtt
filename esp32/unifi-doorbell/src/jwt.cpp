#include "jwt.h"
#include "logging.h"
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <ArduinoJson.h>

// JWT secret stored in memory (loaded from config)
static uint8_t jwtSecret[JWT_SECRET_LEN];
static bool secretInitialized = false;

// Base64url encode (URL-safe base64 without padding)
static String base64UrlEncode(const uint8_t* data, size_t len) {
    size_t outLen = 0;
    mbedtls_base64_encode(NULL, 0, &outLen, data, len);

    char* buf = (char*)malloc(outLen + 1);
    if (!buf) return "";

    mbedtls_base64_encode((unsigned char*)buf, outLen, &outLen, data, len);
    buf[outLen] = 0;

    // Convert to URL-safe: + -> -, / -> _, remove =
    String result = "";
    for (size_t i = 0; i < outLen; i++) {
        if (buf[i] == '+') result += '-';
        else if (buf[i] == '/') result += '_';
        else if (buf[i] == '=') break; // Remove padding
        else result += buf[i];
    }

    free(buf);
    return result;
}

// Base64url decode
static int base64UrlDecode(const String& input, uint8_t* output, size_t* outLen) {
    // Convert from URL-safe back to standard base64
    String b64 = input;
    for (int i = 0; i < b64.length(); i++) {
        if (b64[i] == '-') b64[i] = '+';
        else if (b64[i] == '_') b64[i] = '/';
    }

    // Add padding
    while (b64.length() % 4 != 0) {
        b64 += '=';
    }

    return mbedtls_base64_decode(output, *outLen, outLen,
                                  (const unsigned char*)b64.c_str(), b64.length());
}

// HMAC-SHA256 signature
static String hmacSha256(const String& message, const uint8_t* secret) {
    uint8_t hash[32];

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, secret, JWT_SECRET_LEN);
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)message.c_str(), message.length());
    mbedtls_md_hmac_finish(&ctx, hash);
    mbedtls_md_free(&ctx);

    return base64UrlEncode(hash, 32);
}

void initJwt() {
    if (!secretInitialized) {
        generateJwtSecret();
    }
}

const uint8_t* getJwtSecret() {
    return jwtSecret;
}

void setJwtSecret(const uint8_t* secret) {
    memcpy(jwtSecret, secret, JWT_SECRET_LEN);
    secretInitialized = true;
}

void generateJwtSecret() {
    // Generate cryptographically random secret using ESP32's hardware RNG
    for (int i = 0; i < JWT_SECRET_LEN; i++) {
        jwtSecret[i] = (uint8_t)esp_random();
    }
    secretInitialized = true;
    log("JWT: Generated new secret");
}

String createJwtToken(const String& username) {
    if (!secretInitialized) {
        initJwt();
    }

    // Header (fixed for HS256)
    String header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    String headerB64 = base64UrlEncode((const uint8_t*)header.c_str(), header.length());

    // Payload with username and expiration
    unsigned long now = millis() / 1000;  // Seconds since boot
    unsigned long exp = now + JWT_EXPIRATION_SECONDS;

    JsonDocument doc;
    doc["sub"] = username;
    doc["iat"] = now;
    doc["exp"] = exp;

    String payload;
    serializeJson(doc, payload);
    String payloadB64 = base64UrlEncode((const uint8_t*)payload.c_str(), payload.length());

    // Signature
    String message = headerB64 + "." + payloadB64;
    String signature = hmacSha256(message, jwtSecret);

    return message + "." + signature;
}

String validateJwtToken(const String& token) {
    if (!secretInitialized || token.length() == 0) {
        return "";
    }

    // Split token into parts
    int firstDot = token.indexOf('.');
    int lastDot = token.lastIndexOf('.');

    if (firstDot <= 0 || lastDot <= firstDot || lastDot == token.length() - 1) {
        return ""; // Invalid format
    }

    String headerB64 = token.substring(0, firstDot);
    String payloadB64 = token.substring(firstDot + 1, lastDot);
    String signatureB64 = token.substring(lastDot + 1);

    // Verify signature
    String message = headerB64 + "." + payloadB64;
    String expectedSig = hmacSha256(message, jwtSecret);

    if (signatureB64 != expectedSig) {
        return ""; // Invalid signature
    }

    // Decode payload
    uint8_t payloadBuf[512];
    size_t payloadLen = sizeof(payloadBuf);

    if (base64UrlDecode(payloadB64, payloadBuf, &payloadLen) != 0) {
        return ""; // Decode failed
    }

    payloadBuf[payloadLen] = 0;

    // Parse JSON payload
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, (char*)payloadBuf);
    if (error) {
        return ""; // Parse failed
    }

    // Check expiration
    unsigned long now = millis() / 1000;
    unsigned long exp = doc["exp"] | 0;

    if (exp > 0 && now > exp) {
        return ""; // Token expired
    }

    // Return username
    return doc["sub"].as<String>();
}
