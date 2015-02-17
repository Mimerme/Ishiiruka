// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once
#include <map>
#include <unordered_map>

#include "Common/CommonTypes.h"
#include "Common/Thread.h"

#include "VideoCommon/BPMemory.h"
#include "VideoCommon/TextureDecoder.h"
#include "VideoCommon/VideoCommon.h"

struct VideoConfig;

class TextureCache
{
public:
	enum TexCacheEntryType
	{
		TCET_NORMAL,
		TCET_EC_VRAM,		// EFB copy which sits in VRAM and is ready to be used
		TCET_EC_DYNAMIC,	// EFB copy which sits in RAM and needs to be decoded before being used
	};
	struct TCacheEntryConfig
	{
		TCacheEntryConfig() : width(0), height(0), levels(1), layers(1), rendertarget(false), pcformat(PC_TEX_FMT_NONE) {}

		u32 width, height;
		u32 levels, layers;
		bool rendertarget;
		PC_TexFormat pcformat;
		bool operator == (const TCacheEntryConfig& b) const
		{
			return width == b.width
				&& height == b.height
				&& levels == b.levels
				&& layers == b.layers
				&& rendertarget == b.rendertarget
				&& pcformat == b.pcformat;
		}

		struct Hasher
		{
			size_t operator()(const TextureCache::TCacheEntryConfig& c) const
			{
				return (u64)c.rendertarget << 56	// 1 bit
					| (u64)c.pcformat << 48			// 8 bits
					| (u64)c.layers << 40			// 8 bits 
					| (u64)c.levels << 32			// 8 bits 
					| (u64)c.height << 16			// 16 bits 
					| (u64)c.width;					// 16 bits
			}
		};
	};

	struct TCacheEntryBase
	{
		TCacheEntryConfig config;

		// common members
		u32 addr;
		u32 size_in_bytes;
		u64 hash;
		u32 format;
		enum TexCacheEntryType type;

		u32 native_levels;
		u32 native_width, native_height; // Texture dimensions from the GameCube's point of view
		// used to delete textures which haven't been used for TEXTURE_KILL_THRESHOLD frames
		s32 frameCount;
		bool custom_texture;

		void SetGeneralParameters(u32 _addr, u32 _size, u32 _format)
		{
			addr = _addr;
			size_in_bytes = _size;
			format = _format;
		}

		void SetDimensions(u32 _native_width, u32 _native_height, u32 _native_levels)
		{
			native_width = _native_width;
			native_height = _native_height;
			native_levels = _native_levels;
		}

		void SetHashes(u64 _hash)
		{
			hash = _hash;
		}

		TCacheEntryBase(const TCacheEntryConfig& c) : config(c) {}
		virtual ~TCacheEntryBase();

		virtual void Bind(u32 stage) = 0;
		virtual bool Save(const std::string& filename, u32 level) = 0;

		virtual void Load(const u8* src, u32 width, u32 height,
			u32 expanded_width, u32 level) = 0;
		virtual void Load(const u8* src, u32 width, u32 height, u32 expandedWidth,
			u32 expandedHeight, const s32 texformat, const u32 tlutaddr, const TlutFormat tlutfmt, u32 level) = 0;
		virtual void LoadFromTmem(const u8* ar_src, const u8* gb_src, u32 width, u32 height,
			u32 expanded_width, u32 expanded_Height, u32 level) = 0;
		virtual void FromRenderTarget(u32 dstAddr, unsigned int dstFormat,
			PEControl::PixelFormat srcFormat, const EFBRectangle& srcRect,
			bool isIntensity, bool scaleByHalf, unsigned int cbufid,
			const float *colmat) = 0;
		virtual bool PalettizeFromBase(const TCacheEntryBase* base_entry, s32 texformat) = 0;
		bool OverlapsMemoryRange(u32 range_address, u32 range_size) const;

		bool IsEfbCopy() { return (type == TCET_EC_VRAM || type == TCET_EC_DYNAMIC); }
	};

	virtual ~TextureCache(); // needs virtual for DX11 dtor

	static void OnConfigChanged(VideoConfig& config);
	// Removes textures which aren't used for more than TEXTURE_KILL_THRESHOLD frames,
	// frameCount is the current frame number.
	static void Cleanup(int frameCount);

	static void Invalidate();
	static void InvalidateRange(u32 start_address, u32 size);
	static void MakeRangeDynamic(u32 start_address, u32 size);
	static void ClearRenderTargets();	// currently only used by OGL
	static bool Find(u32 start_address, u64 hash);

	virtual PC_TexFormat GetNativeTextureFormat(const s32 texformat,
		const TlutFormat tlutfmt, u32 width, u32 height) = 0;
	virtual TCacheEntryBase* CreateTexture(const TCacheEntryConfig& config) = 0;

	static TCacheEntryBase* Load(const u32 stage);
	static void CopyRenderTargetToTexture(u32 dstAddr, u32 dstFormat, PEControl::PixelFormat srcFormat,
		const EFBRectangle& srcRect, bool isIntensity, bool scaleByHalf);

	static void RequestInvalidateTextureCache();

	virtual void LoadLut(u32 lutFmt, void* addr, u32 size) = 0;

protected:
	static GC_ALIGNED16(u8 *temp);
	static size_t temp_size;
	TextureCache();
private:
	static void CheckTempSize(size_t required_size);
	static void DumpTexture(TCacheEntryBase* entry, std::string basename, u32 level);
	static u32 s_prev_tlut_address;
	static u32 s_prev_tlut_size;
	static u64 s_prev_tlut_hash;
	static TCacheEntryBase* AllocateTexture(const TCacheEntryConfig& config);
	static void FreeTexture(TCacheEntryBase* entry);

	typedef std::map<u64, TCacheEntryBase*> TexGroupCache;
	typedef std::map<u32, TexGroupCache> TexCache;
	typedef std::unordered_multimap<TCacheEntryConfig, TCacheEntryBase*, TCacheEntryConfig::Hasher> TexPool;

	static TexCache textures;
	static TexPool texture_pool;
	// Backup configuration values
	static struct BackupConfig
	{
		s32 s_colorsamples;
		bool s_texfmt_overlay;
		bool s_texfmt_overlay_center;
		bool s_hires_textures;
		bool s_copy_cache_enable;
	} backup_config;
};

extern TextureCache *g_texture_cache;