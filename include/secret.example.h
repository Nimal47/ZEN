// ============================================================
//  secret.example.h — ZEN / ADIS Credential Template
//
//  SETUP INSTRUCTIONS:
//  1. Copy this file:  cp secret.example.h include/secret.h
//  2. Fill in your actual values below
//  3. Never commit secret.h — it is already in .gitignore
// ============================================================

#ifndef SECRET_H
#define SECRET_H

// ------------------------------------------------------------
// WiFi Credentials
// ------------------------------------------------------------
#define WIFI_SSID       "your_wifi_network_name"
#define WIFI_PASSWORD   "your_wifi_password"

// ------------------------------------------------------------
// Firebase Configuration
// Get these from:
//   Firebase Console → Project Settings → General
// ------------------------------------------------------------

// Web API Key (Project Settings → General → Web API Key)
#define FIREBASE_API_KEY        "your_firebase_api_key"

// Realtime Database URL (Realtime Database → Data tab)
// Format: https://your-project-id-default-rtdb.firebaseio.com/
#define FIREBASE_DATABASE_URL   "https://your-project-id-default-rtdb.firebaseio.com/"

// ------------------------------------------------------------
// Firebase Auth (if using email/password authentication)
// Leave blank if using open rules during development
// ------------------------------------------------------------
#define FIREBASE_USER_EMAIL     "your_firebase_user@email.com"
#define FIREBASE_USER_PASSWORD  "your_firebase_user_password"

#endif // SECRET_H
