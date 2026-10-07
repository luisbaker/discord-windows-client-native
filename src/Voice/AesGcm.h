#pragma once

#include <bcrypt.h>

namespace DiscordWin3::Voice
{
    // AES-256-GCM via Windows CNG (no OpenSSL needed for the transport layer).
    // Used for Discord's "aead_aes256_gcm_rtpsize" RTP encryption mode.
    class AesGcm
    {
    public:
        AesGcm() = default;
        AesGcm(AesGcm const&) = delete;
        AesGcm& operator=(AesGcm const&) = delete;
        ~AesGcm();

        bool SetKey(uint8_t const* key, size_t length);
        bool HasKey() const { return m_key != nullptr; }

        // nonce: 12 bytes. out receives `length` bytes, tag receives 16 bytes.
        bool Encrypt(uint8_t const* nonce, uint8_t const* aad, size_t aadLength,
                     uint8_t const* plain, size_t length, uint8_t* out, uint8_t* tag);
        bool Decrypt(uint8_t const* nonce, uint8_t const* aad, size_t aadLength,
                     uint8_t const* cipher, size_t length, uint8_t const* tag, uint8_t* out);

    private:
        void Close();
        BCRYPT_ALG_HANDLE m_alg = nullptr;
        BCRYPT_KEY_HANDLE m_key = nullptr;
    };
}
