#include "pch.h"
#include "Fonts.h"
#include "Strings.h"

#include <dwrite_3.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <filesystem>
#include <fstream>
#include <thread>

#pragma comment(lib, "dwrite.lib")

using namespace winrt;

namespace DiscordWin3::Fonts
{
    namespace
    {
        // @font-face sources of the official web client (normal 400-800, italic 400/600/700).
        constexpr wchar_t const* Files[] = {
            L"66d715454104d24e", L"b272b33815319bae", L"2df2c3ff74408972", L"189422196a4f8b53", L"b2fdbe507d6ce9ef",
            L"dd24010f3cf7def7", L"d5d789aeb6282532", L"ce3b8055f5114434",
        };

        // XAML only loads font files through ms-appx:/// (the exe folder) in an unpackaged app.
        std::filesystem::path FontPath()
        {
            wchar_t exe[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            return std::filesystem::path{ exe }.parent_path() / L"Fonts" / L"ggsans.ttc";
        }

        uint32_t Be32(uint8_t const* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
        uint16_t Be16(uint8_t const* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
        void Put32(std::vector<uint8_t>& out, size_t at, uint32_t v)
        {
            out[at] = uint8_t(v >> 24); out[at + 1] = uint8_t(v >> 16); out[at + 2] = uint8_t(v >> 8); out[at + 3] = uint8_t(v);
        }

        // WOFF2 -> plain sfnt (TrueType/OpenType) bytes through DirectWrite.
        std::vector<uint8_t> Unpack(IDWriteFactory5* factory, std::vector<uint8_t> const& woff2)
        {
            com_ptr<IDWriteFontFileStream> stream;
            if (FAILED(factory->UnpackFontFile(DWRITE_CONTAINER_TYPE_WOFF2, woff2.data(), static_cast<UINT32>(woff2.size()), stream.put())))
                return {};
            UINT64 size = 0;
            stream->GetFileSize(&size);
            void const* data = nullptr;
            void* context = nullptr;
            if (FAILED(stream->ReadFileFragment(&data, 0, size, &context))) return {};
            std::vector<uint8_t> out(static_cast<uint8_t const*>(data), static_cast<uint8_t const*>(data) + size);
            stream->ReleaseFileFragment(context);
            return out;
        }

        // Several sfnt fonts -> one TrueType Collection, so a single FontFamily covers every weight.
        std::vector<uint8_t> BuildCollection(std::vector<std::vector<uint8_t>> const& fonts)
        {
            std::vector<uint8_t> out(12 + 4 * fonts.size());
            out[0] = 't'; out[1] = 't'; out[2] = 'c'; out[3] = 'f';
            Put32(out, 4, 0x00010000);
            Put32(out, 8, static_cast<uint32_t>(fonts.size()));

            // Directories first (fixed size), table data after them.
            std::vector<size_t> dirAt;
            for (size_t i = 0; i < fonts.size(); ++i)
            {
                auto const& f = fonts[i];
                uint16_t tables = Be16(f.data() + 4);
                Put32(out, 12 + 4 * i, static_cast<uint32_t>(out.size()));
                dirAt.push_back(out.size());
                out.insert(out.end(), f.begin(), f.begin() + 12 + 16 * tables);
            }
            for (size_t i = 0; i < fonts.size(); ++i)
            {
                auto const& f = fonts[i];
                uint16_t tables = Be16(f.data() + 4);
                for (uint16_t t = 0; t < tables; ++t)
                {
                    uint8_t const* record = f.data() + 12 + 16 * t;
                    uint32_t offset = Be32(record + 8), length = Be32(record + 12);
                    if (size_t(offset) + length > f.size()) return {};
                    while (out.size() % 4) out.push_back(0);
                    Put32(out, dirAt[i] + 12 + 16 * t + 8, static_cast<uint32_t>(out.size()));
                    out.insert(out.end(), f.begin() + offset, f.begin() + offset + length);
                }
            }
            return out;
        }

        void Download(std::filesystem::path target)
        {
            try
            {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                com_ptr<IDWriteFactory5> factory;
                check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory5),
                                                  reinterpret_cast<::IUnknown**>(factory.put())));
                Windows::Web::Http::HttpClient http;
                std::vector<std::vector<uint8_t>> fonts;
                for (auto file : Files)
                {
                    auto buffer = http.GetBufferAsync(Windows::Foundation::Uri{ std::wstring{ L"https://discord.com/assets/" } + file + L".woff2" }).get();
                    std::vector<uint8_t> woff2(buffer.data(), buffer.data() + buffer.Length());
                    auto sfnt = Unpack(factory.get(), woff2);
                    if (sfnt.size() < 12) return;   // asset renamed by a Discord deploy: keep Segoe UI
                    fonts.push_back(std::move(sfnt));
                }
                auto collection = BuildCollection(fonts);
                if (collection.empty()) return;
                std::error_code ec;
                std::filesystem::create_directories(target.parent_path(), ec);
                auto temp = target;
                temp += L".tmp";
                {
                    std::ofstream file{ temp, std::ios::binary };
                    file.write(reinterpret_cast<char const*>(collection.data()), static_cast<std::streamsize>(collection.size()));
                }
                std::filesystem::rename(temp, target, ec);
            }
            catch (...)
            {
            }
        }
    }

    void Initialize()
    {
        auto path = FontPath();
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            std::thread{ Download, path }.detach();
            return;
        }
        // Every default control template reads these keys: one swap restyles the whole UI.
        Microsoft::UI::Xaml::Media::FontFamily family{ L"ms-appx:///Fonts/ggsans.ttc#gg sans" };
        auto resources = Microsoft::UI::Xaml::Application::Current().Resources();
        resources.Insert(box_value(L"ContentControlThemeFontFamily"), family);
        resources.Insert(box_value(L"XamlAutoFontFamily"), family);
        resources.Insert(box_value(L"AppFont"), family);
        // Text elements outside controls (TextBlock / RichTextBlock in templates): implicit styles.
        using namespace Microsoft::UI::Xaml;
        auto implicitStyle = [&](winrt::Windows::UI::Xaml::Interop::TypeName type, DependencyProperty const& property)
        {
            Style style{ type };
            style.Setters().Append(Setter{ property, family });
            resources.Insert(box_value(type), style);
        };
        implicitStyle(winrt::xaml_typename<Controls::TextBlock>(), Controls::TextBlock::FontFamilyProperty());
        implicitStyle(winrt::xaml_typename<Controls::RichTextBlock>(), Controls::RichTextBlock::FontFamilyProperty());
    }
}
