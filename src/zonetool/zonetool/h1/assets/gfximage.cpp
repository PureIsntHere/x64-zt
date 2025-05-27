#include <std_include.hpp>
#include "gfximage.hpp"

#pragma warning( push )
#pragma warning( disable : 4459 )
#include <DirectXTex.h>
#pragma warning( pop )

#include "zonetool/utils/iwi.hpp"
#include "zonetool/utils/compression.hpp"

#include <utils/cryptography.hpp>
#include <utils/flags.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>

#include <filesystem>
#include <vector>
#include <fstream>

namespace zonetool::h1
{
    // Helper to detect special map images
    namespace
    {
        bool is_map_image(const std::string& name)
        {
            if (name.size() < 6) return false;
            auto p = name.substr(0, 6);
            return p == "*light" || p == "*refle" || name == "$outdoor";
        }

        std::string clean_name(const std::string& name)
        {
            std::string s = name;
            for (auto& c : s)
                if (c == '*') c = '_';
            return s;
        }
    }

    // Flags for loaded images
    namespace
    {
        void add_loaded_image_flags(GfxImage* image)
        {
            if (image->levelCount <= 1)
                image->flags |= IMAGE_FLAG_NOMIPMAPS;
            if (image->numElements > 1 && image->mapType != MAPTYPE_CUBE)
                image->mapType = MAPTYPE_ARRAY;
        }
    }

    // Parse IWI-encoded images
    namespace iwi
    {
        GfxImage* parse(const std::string& name, zone_memory* mem)
        {
            ::iwi::GfxImage tmp{};
            memset(&tmp, 0, sizeof(tmp));
            auto ret = ::iwi::parse_iwi(name, mem, &tmp);
            if (!ret) return nullptr;

            auto* img = mem->allocate<GfxImage>();
            img->name = ret->name;
            img->imageFormat = ret->imageFormat;
            img->mapType = static_cast<MapType>(ret->mapType);
            img->dataLen1 = ret->dataLen;
            img->dataLen2 = ret->dataLen;
            img->width = ret->width;
            img->height = ret->height;
            img->depth = ret->depth;
            img->numElements = ret->numElements;
            img->levelCount = ret->levelCount;
            img->pixelData = ret->pixelData;
            img->streamed = false;
            img->semantic = TS_COLOR_MAP;
            img->category = IMG_CATEGORY_LOAD_FROM_FILE;
            add_loaded_image_flags(img);
            return img;
        }
    }

    // Parse DDS/TGA/PNG via DirectXTex
    namespace directxtex
    {
        bool load_image(const std::string& name, DirectX::ScratchImage* out)
        {
            std::string base = utils::string::va("images\\%s", clean_name(name).c_str());
            std::string ext;
            if (filesystem::file(base + ".dds").exists()) ext = ".dds";
            else if (filesystem::file(base + ".tga").exists()) ext = ".tga";
            else if (filesystem::file(base + ".png").exists()) ext = ".png";
            else return false;

            std::string path = filesystem::get_file_path(base + ext) + base + ext;
            std::wstring w = utils::string::convert(path);
            HRESULT hr = E_FAIL;
            if (ext == ".dds") hr = DirectX::LoadFromDDSFile(w.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, *out);
            else if (ext == ".tga") hr = DirectX::LoadFromTGAFile(w.c_str(), nullptr, *out);
            else if (ext == ".png") hr = DirectX::LoadFromWICFile(w.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, *out);
            return SUCCEEDED(hr);
        }

        GfxImage* parse(const std::string& name, zone_memory* mem)
        {
            DirectX::ScratchImage scratch;
            if (!load_image(name, &scratch)) return nullptr;

            ZONETOOL_INFO("Parsing custom image \"%s\"", name.c_str());
            auto& meta = scratch.GetMetadata();
            auto* pix = scratch.GetImages();
            size_t sz = scratch.GetPixelsSize();

            auto* img = mem->allocate<GfxImage>();
            img->imageFormat = meta.format;
            img->mapType = static_cast<MapType>(meta.dimension);
            img->semantic = TS_COLOR_MAP;
            img->category = IMG_CATEGORY_LOAD_FROM_FILE;
            img->width = static_cast<unsigned short>(meta.width);
            img->height = static_cast<unsigned short>(meta.height);
            img->depth = static_cast<unsigned short>(meta.depth);
            img->numElements = static_cast<unsigned short>(meta.arraySize);
            img->levelCount = static_cast<unsigned char>(meta.mipLevels);
            img->streamed = false;
            img->dataLen1 = static_cast<int>(sz);
            img->dataLen2 = static_cast<int>(sz);
            img->pixelData = mem->allocate<unsigned char>(sz);
            memcpy(img->pixelData, pix->pixels, sz);
            img->name = mem->duplicate_string(name.c_str());

            if (meta.IsCubemap())
            {
                img->mapType = MAPTYPE_CUBE;
                img->numElements = 1;
            }
            add_loaded_image_flags(img);
            return img;
        }
    }

    // Try DirectXTex, then IWI
    GfxImage* gfx_image::parse_custom(const std::string& name, zone_memory* mem)
    {
        if (auto* i = directxtex::parse(name, mem)) { is_iwi = false; return i; }
        if (auto* i = iwi::parse(name, mem)) { is_iwi = true;  return i; }
        return nullptr;
    }

    // Streamed-image helpers
    std::optional<std::string> get_streamed_image_pixels(const std::string& name, int stream)
    {
        auto p = utils::string::va("streamed_images\\%s_stream%i.pixels", clean_name(name).c_str(), stream);
        auto full = filesystem::get_file_path(p) + p;
        if (utils::io::file_exists(full))
            return utils::io::read_file(full);
        return std::nullopt;
    }

    bool get_streamed_image_dds(const std::string& name, int stream, DirectX::ScratchImage& out)
    {
        auto p = utils::string::va("streamed_images\\%s_stream%i.dds", clean_name(name).c_str(), stream);
        auto full = filesystem::get_file_path(p) + p;
        auto w = utils::string::convert(full);
        return SUCCEEDED(DirectX::LoadFromDDSFile(w.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, out));
    }

    std::optional<std::string> get_streamed_image_dds(const std::string& name, int stream)
    {
        DirectX::ScratchImage scratch;
        if (!get_streamed_image_dds(name, stream, scratch))
            return std::nullopt;
        auto* pix = scratch.GetImages();
        size_t sz = scratch.GetPixelsSize();
        return std::string(reinterpret_cast<const char*>(pix->pixels), sz);
    }

    std::optional<std::string> get_streamed_image_pixels_path(const std::string& name, int stream)
    {
        auto p = utils::string::va("streamed_images\\%s_stream%i.pixels", clean_name(name).c_str(), stream);
        auto full = filesystem::get_file_path(p) + p;
        if (utils::io::file_exists(full))
            return full;
        return std::nullopt;
    }

    // Parse a streamed h1Image container (legacy)
    GfxImage* gfx_image::parse_streamed_image(const std::string& name, zone_memory* mem)
    {
        auto p = utils::string::va("streamed_images\\%s.h1Image", clean_name(name).c_str());
        assetmanager::reader read(mem);
        if (!read.open(p)) return nullptr;

        auto* img = read.read_single<GfxImage>();
        img->name = read.read_string();
        ZONETOOL_INFO("Parsing streamed image \"%s\"...", name.c_str());
        custom_streamed_image = true;
        img->streamed = true;
        for (int i = 0; i < 4; ++i)
        {
            if (auto path = get_streamed_image_pixels_path(name, i))
                image_stream_blocks_paths[i] = *path;
        }
        read.close();
        return img;
    }

    // Fallback parse from `.dds` instead of `.h1Image`
    GfxImage* gfx_image::parse(const std::string& name, zone_memory* mem)
    {
        auto path = filesystem::get_file_path("") + "images\\" + clean_name(name) + ".dds";
        assetmanager::reader read(mem);
        if (!read.open(path)) return nullptr;

        ZONETOOL_INFO("Parsing image \"%s\"", name.c_str());
        auto* img = read.read_single<GfxImage>();
        img->name = read.read_string();
        if (img->pixelData)
            img->pixelData = read.read_array<unsigned char>();
        read.close();
        return img;
    }

    // Initialize the gfx_image asset
    void gfx_image::init(const std::string& name, zone_memory* mem)
    {
        name_ = name;
        if (referenced())
        {
            asset_ = mem->allocate<std::remove_reference_t<decltype(*asset_)>>();
            asset_->name = mem->duplicate_string(name);
            return;
        }
        if (auto* i = parse(name, mem)) { asset_ = i; return; }
        if (auto* s = parse_streamed_image(name, mem)) { asset_ = s; return; }
        if (auto* c = parse_custom(name, mem)) { asset_ = c; return; }

        // fallback default pixel
        ZONETOOL_WARNING("Image \"%s\" not found, using default", name.c_str());
        static unsigned char def[4] = { 255,0,0,255 };
        auto* img = mem->allocate<GfxImage>();
        img->imageFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        img->mapType = MAPTYPE_2D;
        img->semantic = TS_2D;
        img->category = IMG_CATEGORY_AUTO_GENERATED;
        img->flags = 0;
        img->dataLen1 = 4;
        img->dataLen2 = 4;
        img->width = 1;
        img->height = 1;
        img->depth = 1;
        img->numElements = 1;
        img->levelCount = 1;
        img->streamed = false;
        img->pixelData = def;
        img->name = mem->duplicate_string(name);
        asset_ = img;
    }

    void gfx_image::init(void* asset, zone_memory* mem)
    {
        asset_ = reinterpret_cast<GfxImage*>(asset);
        name_ = asset_->name;
        if (auto* i = parse(name_, mem))
            asset_ = i;
    }

    void gfx_image::prepare(zone_buffer* buf, zone_memory* mem) {}
    void gfx_image::load_depending(zone_base* zone) {}
    std::string gfx_image::name() { return name_; }
    int32_t     gfx_image::type() { return ASSET_TYPE_IMAGE; }

    void gfx_image::write(zone_base* zone, zone_buffer* buf)
    {
        auto data = asset_;
        auto dest = buf->write(data);
        buf->push_stream(XFILE_BLOCK_VIRTUAL);
        dest->name = buf->write_str(name_);
        buf->push_stream(XFILE_BLOCK_TEMP);
        if (data->pixelData)
        {
            buf->align(3);
            buf->write_stream(data->pixelData, data->dataLen1);
            buf->clear_pointer(&dest->pixelData);
        }
        buf->pop_stream();
        buf->pop_stream();
        if (data->streamed)
        {
            for (int i = 0; i < 4; ++i)
                buf->write_streamfile(reinterpret_cast<uintptr_t>(image_stream_files[i]));
        }
    }

    // Dumps only to DDS, removes .h1Image fallback
    void dump_streamed_image(GfxImage* image, bool isSelf, bool dumpDDS)
    {
        for (int i = 0; i < 4; ++i)
        {
            const auto& sf = stream_files[*stream_file_index + i];
            if (!sf.fileIndex || sf.offset == 0 || sf.offsetEnd == 0) continue;

            auto pak = isSelf ? filesystem::get_fastfile() : utils::string::va("imagefile%d.pak", sf.fileIndex);
            auto dir = filesystem::get_zone_path(pak);
            auto path = dir + pak;
            std::ifstream in(path, std::ios::binary);
            if (!in) continue;
            in.seekg(sf.offset);
            std::vector<char> buf(sf.offsetEnd - sf.offset);
            in.read(buf.data(), buf.size());
            auto comp = std::string(buf.data(), buf.size());
            auto dec = compression::lz4::decompress_lz4_block(comp);
            std::vector<uint8_t> pixels(dec.begin(), dec.end());

            auto outdir = filesystem::get_dump_path() + "streamed_images\\";
            std::filesystem::create_directories(outdir);
            auto raw = utils::string::va("%s%s_stream%i.pixels", outdir.c_str(), clean_name(image->name).c_str(), i);
            utils::io::write_file(raw, dec, false);

            if (dumpDDS)
            {
                DirectX::Image img{};
                img.format = DXGI_FORMAT(image->imageFormat);
                img.width = image->streams[i].width;
                img.height = image->streams[i].height;
                size_t r, s;
                DirectX::ComputePitch(img.format, img.width, img.height, r, s);
                img.rowPitch = r;
                img.slicePitch = s;
                img.pixels = pixels.data();

                auto ddsPath = outdir + clean_name(image->name) + "_stream" + std::to_string(i) + ".dds";
                auto wdds = utils::string::convert(ddsPath);
                DirectX::SaveToDDSFile(img, DirectX::DDS_FLAGS_NONE, wdds.c_str());
            }
        }
    }

    void dump_image_dds(GfxImage* image)
    {
        std::vector<DirectX::Image> faces;
        int sides = (image->mapType == MAPTYPE_CUBE) ? 6 : 1;
        const uint8_t* ptr = image->pixelData;
        for (int e = 0; e < image->numElements; ++e)
        {
            for (int f = 0; f < sides; ++f)
            {
                for (int m = 0; m < image->levelCount; ++m)
                {
                    DirectX::Image I{};
                    I.format = DXGI_FORMAT(image->imageFormat);
                    I.width = image->width >> m;
                    I.height = image->height >> m;
                    size_t r, s;
                    DirectX::ComputePitch(I.format, I.width, I.height, r, s);
                    I.rowPitch = r;
                    I.slicePitch = s;
                    I.pixels = const_cast<uint8_t*>(ptr);
                    ptr += s;
                    faces.push_back(I);
                }
            }
        }

        DirectX::TexMetadata meta{};
        meta.width = image->width;
        meta.height = image->height;
        meta.depth = image->depth;
        meta.arraySize = image->numElements * sides;
        meta.mipLevels = image->levelCount;
        meta.format = DXGI_FORMAT(image->imageFormat);
        if (image->mapType == MAPTYPE_CUBE)
        {
            meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
            meta.miscFlags = DirectX::TEX_MISC_TEXTURECUBE;
        }
        else
        {
            meta.dimension = static_cast<DirectX::TEX_DIMENSION>(image->mapType);
        }

        auto outdir = filesystem::get_dump_path() + "images\\";
        std::filesystem::create_directories(outdir);
        auto ddsPath = outdir + clean_name(image->name) + ".dds";
        auto wdds = utils::string::convert(ddsPath);
        DirectX::SaveToDDSFile(faces.data(), faces.size(), meta, DirectX::DDS_FLAGS_NONE, wdds.c_str());
    }

    void gfx_image::dump(GfxImage* asset)
    {
        // Always dump DDS
        dump_image_dds(asset);

        // Then handle streamed
        if (asset->streamed)
        {
            bool isSelf = (stream_files[*stream_file_index].fileIndex == 96);
            dump_streamed_image(asset, isSelf, true);
        }
    }

} // namespace zonetool::h1
