#include "pch.h"
#include "AesGcm.h"

namespace DiscordWin3::Voice
{
    AesGcm::~AesGcm()
    {
        Close();
    }

    void AesGcm::Close()
    {
        if (m_key) BCryptDestroyKey(m_key);
        if (m_alg) BCryptCloseAlgorithmProvider(m_alg, 0);
        m_key = nullptr;
        m_alg = nullptr;
    }

    bool AesGcm::SetKey(uint8_t const* key, size_t length)
    {
        Close();
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&m_alg, BCRYPT_AES_ALGORITHM, nullptr, 0))) return false;
        if (!BCRYPT_SUCCESS(BCryptSetProperty(m_alg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                                              sizeof(BCRYPT_CHAIN_MODE_GCM), 0)))
        {
            Close();
            return false;
        }
        if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(m_alg, &m_key, nullptr, 0, const_cast<PUCHAR>(key), static_cast<ULONG>(length), 0)))
        {
            Close();
            return false;
        }
        return true;
    }

    bool AesGcm::Encrypt(uint8_t const* nonce, uint8_t const* aad, size_t aadLength,
                         uint8_t const* plain, size_t length, uint8_t* out, uint8_t* tag)
    {
        if (!m_key) return false;
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = const_cast<PUCHAR>(nonce);
        info.cbNonce = 12;
        info.pbAuthData = const_cast<PUCHAR>(aad);
        info.cbAuthData = static_cast<ULONG>(aadLength);
        info.pbTag = tag;
        info.cbTag = 16;
        ULONG written = 0;
        return BCRYPT_SUCCESS(BCryptEncrypt(m_key, const_cast<PUCHAR>(plain), static_cast<ULONG>(length), &info,
                                            nullptr, 0, out, static_cast<ULONG>(length), &written, 0));
    }

    bool AesGcm::Decrypt(uint8_t const* nonce, uint8_t const* aad, size_t aadLength,
                         uint8_t const* cipher, size_t length, uint8_t const* tag, uint8_t* out)
    {
        if (!m_key) return false;
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = const_cast<PUCHAR>(nonce);
        info.cbNonce = 12;
        info.pbAuthData = const_cast<PUCHAR>(aad);
        info.cbAuthData = static_cast<ULONG>(aadLength);
        info.pbTag = const_cast<PUCHAR>(tag);
        info.cbTag = 16;
        ULONG written = 0;
        return BCRYPT_SUCCESS(BCryptDecrypt(m_key, const_cast<PUCHAR>(cipher), static_cast<ULONG>(length), &info,
                                            nullptr, 0, out, static_cast<ULONG>(length), &written, 0));
    }
}
