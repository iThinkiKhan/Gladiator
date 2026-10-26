#pragma once
// Template for shared/secrets.h (gitignored). Copy this file to secrets.h and
// replace the values. Without secrets.h the build falls back to this file, so
// firmware built from a fresh checkout uses these placeholders.
#define GLADIATOR_FIELD_PASSWORD "change-me-field"
#define GLADIATOR_MAINTENANCE_AP_PASSWORD "change-me-maint"
#define GLADIATOR_OTA_PASSWORD "change-me-ota"

// Set an identical, unique key on both S3 and C6.
#define GLADIATOR_LINK_KEY "replace-with-unique-shared-link-key"
