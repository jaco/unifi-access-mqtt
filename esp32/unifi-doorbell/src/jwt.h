#pragma once

#include <Arduino.h>

// JWT secret length (32 bytes = 256 bits)
#define JWT_SECRET_LEN 32

// JWT token expiration (24 hours in seconds)
#define JWT_EXPIRATION_SECONDS 86400

// Initialize JWT system, generating a secret if needed
void initJwt();

// Get the JWT secret (for storage in config)
const uint8_t* getJwtSecret();

// Set the JWT secret (loaded from config)
void setJwtSecret(const uint8_t* secret);

// Generate a new random secret
void generateJwtSecret();

// Create a JWT token for a user
String createJwtToken(const String& username);

// Validate a JWT token, returns username if valid, empty string if invalid
String validateJwtToken(const String& token);
