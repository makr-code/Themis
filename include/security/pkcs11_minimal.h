#pragma once
// Minimal PKCS#11 type/constant declarations to avoid external header dependency.
// This is NOT a full PKCS#11 header; only what we need for basic sign/verify.
// For production replace with the vendor's official pkcs11.h.

#include <cstdint>
#include <cstddef>

extern "C" {

typedef uint32_t CK_RV;      // return value
typedef uint32_t CK_SLOT_ID;  // slot identifier
typedef uint32_t CK_SESSION_HANDLE; // session handle
typedef uint32_t CK_OBJECT_HANDLE;  // object handle

typedef uint8_t CK_BYTE;
typedef CK_BYTE* CK_BYTE_PTR;

typedef struct CK_MECHANISM {
    uint32_t mechanism; // Mechanism type
    void*    pParameter;
    size_t   ulParameterLen;
} CK_MECHANISM;

typedef struct CK_ATTRIBUTE {
    uint32_t type;       // Attribute type (e.g. CKA_CLASS)
    void*    pValue;     // Pointer to value buffer
    size_t   ulValueLen; // Value length (in/out)
} CK_ATTRIBUTE; 

// Mechanism constants (subset)
#define CKM_RSA_PKCS 0x00000001U
#define CKM_SHA256_RSA_PKCS 0x00000040U
#define CKM_ECDSA 0x00001041U

// Object classes (subset)
#define CKO_PRIVATE_KEY 0x00000003U
#define CKO_PUBLIC_KEY  0x00000002U
#define CKO_CERTIFICATE 0x00000001U

// Attribute types (subset)
#define CKA_CLASS       0x00000000U
#define CKA_LABEL       0x00000003U
#define CKA_VALUE       0x00000011U

// Return values (subset)
#define CKR_OK                  0x00000000U
#define CKR_GENERAL_ERROR       0x00000005U
#define CKR_DEVICE_ERROR        0x00000030U
#define CKR_PIN_INCORRECT       0x000000A0U
#define CKR_ARGUMENTS_BAD       0x00000007U
#define CKR_SIGNATURE_INVALID   0x000000C0U

// Session flags (subset)
#define CKF_SERIAL_SESSION 0x00000004U

// User types
#define CKU_USER 1U

// Function list forward declaration
struct CK_FUNCTION_LIST;
typedef CK_FUNCTION_LIST* CK_FUNCTION_LIST_PTR;

typedef CK_RV (*CK_C_GetFunctionList)(CK_FUNCTION_LIST_PTR*);

// Function list structure (subset of pointers)
struct CK_FUNCTION_LIST {
    CK_RV (*C_Initialize)(void*);
    CK_RV (*C_Finalize)(void*);
    CK_RV (*C_GetSlotList)(uint8_t, CK_SLOT_ID*, uint32_t*);
    CK_RV (*C_OpenSession)(CK_SLOT_ID, uint32_t, void*, void*, CK_SESSION_HANDLE*);
    CK_RV (*C_CloseSession)(CK_SESSION_HANDLE);
    CK_RV (*C_Login)(CK_SESSION_HANDLE, uint32_t, CK_BYTE_PTR, uint32_t);
    CK_RV (*C_Logout)(CK_SESSION_HANDLE);
    CK_RV (*C_FindObjectsInit)(CK_SESSION_HANDLE, CK_ATTRIBUTE*, uint32_t);
    CK_RV (*C_FindObjects)(CK_SESSION_HANDLE, CK_OBJECT_HANDLE*, uint32_t, uint32_t*);
    CK_RV (*C_FindObjectsFinal)(CK_SESSION_HANDLE);
    CK_RV (*C_SignInit)(CK_SESSION_HANDLE, CK_MECHANISM*, CK_OBJECT_HANDLE);
    CK_RV (*C_Sign)(CK_SESSION_HANDLE, CK_BYTE_PTR, uint32_t, CK_BYTE_PTR, uint32_t*);
    CK_RV (*C_VerifyInit)(CK_SESSION_HANDLE, CK_MECHANISM*, CK_OBJECT_HANDLE);
    CK_RV (*C_Verify)(CK_SESSION_HANDLE, CK_BYTE_PTR, uint32_t, CK_BYTE_PTR, uint32_t);
    CK_RV (*C_GetAttributeValue)(CK_SESSION_HANDLE, CK_OBJECT_HANDLE, CK_ATTRIBUTE*, uint32_t);
};

} // extern "C"
