// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Core/HW/Memmap.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoBackends/DX11/D3DPtr.h"
#include "VideoBackends/DX11/D3DBase.h"
#include "VideoBackends/DX11/D3DShader.h"
#include "VideoBackends/DX11/D3DUtil.h"
#include "VideoBackends/DX11/FramebufferManager.h"
#include "VideoBackends/DX11/D3DState.h"
#include "VideoBackends/DX11/CSTextureDecoder.h"
#include "VideoBackends/DX11/Render.h"
#include "VideoBackends/DX11/TextureCache.h"
#include "VideoBackends/DX11/VertexShaderCache.h"


namespace DX11
{


static const char DECODER_CS[] = R"HLSL(
//

Buffer<uint> rawData_ : register(t0);
Buffer<uint> lutData_ : register(t1);
Texture2D<float4> sourceData_ : register(t2);

RWTexture2D<uint4> dstTexture_ : register(u0);

uint2 dims_ : register(b0);

uint ReadByte( uint addr ) {
	uint r = rawData_[addr>>2];

	return (r >> (8*(addr&0x3)))&0xff;
}

uint ReadTwoBytes( uint addr ) {
	uint r = rawData_[addr>>2];
	r = (r >> (16*((addr>>1)&0x1)))&0xffff;
	return ((r&0xff)<<8)| ((r&0xff00)>>8);
}

uint ReadFourBytes( uint addr ) {
	uint r = rawData_[addr>>2];
	
	return ((r&0xff)<<24) | ((r&0xff00)<<8) | ((r&0xff000000)>>24) | ((r&0xff0000)>>8);
}

uint ReadFourBytesNoSwap( uint addr ) {
	uint r = rawData_[addr>>2];
	
	return r;
}

#ifndef DECODER_FUNC
#error DECODER_FUNC not set
#endif

#ifndef LUT_FUNC
#error LUT_FUNC not set
#endif

uint4 Read5A3( uint v ) {
	if (v&0x8000) {
		uint r = (v&0x7C00)>>7; r |= r>>5;
		uint g = (v&0x03E0)>>2;	g |= g>>5;
		uint b = (v&0x001F)<<3;	b |= b>>5;
		return uint4(r,g,b,255);
	} else {
		uint a = (v&0x7000)>>7; a |= (a>>3) | (a>>6);
		uint r = (v&0x0F00)>>4; r |= r>>4;
		uint g = (v&0x00F0)>>0; g |= g>>4;
		uint b = (v&0x000F)<<4; b |= b>>4;
		return uint4(r,g,b,a);
	}
}

uint4 Read565( uint v ) {
	uint b = (v&0x1F)<<3; b |= b >> 5;
	uint g = (v&0x7E0)>>3; g |= g >> 6;
	uint r = (v&0xF800)>>8; r |= r >> 5;
  return uint4(r,g,b,255);
}


uint4 ReadLutIA8( uint idx ) {
	uint v = lutData_[idx];
	v = ((v&0xff)<<8)| ((v&0xff00)>>8);
	return uint4( uint(v&0xff).xxx, (v&0xff00)>>8 );
}

uint4 ReadLut565( uint idx ) {
	uint v = lutData_[idx];
	v = ((v&0xff)<<8)| ((v&0xff00)>>8);
  return Read565(v);
}

uint4 ReadLut5A3( uint idx ) {
	uint v = lutData_[idx];
	v = ((v&0xff)<<8)| ((v&0xff00)>>8);
	return Read5A3(v);
}

uint Decode2Nibbles( uint2 st ) {
	uint pitch = ((dims_.x+7)/8)*32;
	uint tile = (st.y/8) * pitch + 32 * (st.x/8);
  uint offs = (st.x&0x7) + (st.y&0x7)*4;
	return ReadByte( tile + offs );
}

uint Decode1Byte( uint2 st ) {
	uint pitch = ((dims_.x+7)/8)*32;
	uint tile = (st.y/4) * pitch + 32 * (st.x/8);
  uint offs = (st.x&0x7) + (st.y&0x3)*8;
	return ReadByte( tile + offs );
}

uint Decode2Bytes( uint2 st ) {
	uint pitch = ((dims_.x+3)/4)*32;
	uint tile = (st.y/4) * pitch + 32 * (st.x/4);
  uint offs = (st.x&0x3)*2 + (st.y&0x3)*8;
	return ReadTwoBytes( tile + offs );
}

uint4 DecodeI8( uint2 st ) {
	uint v = Decode1Byte( st);
  return v.xxxx;
}

uint4 DecodeC8( uint2 st ) {
	uint v = Decode1Byte(st);
	return LUT_FUNC(v);
}

uint4 DecodeC14( uint2 st ) {
	uint v = Decode1Byte(st);
	return LUT_FUNC(v&0x3FFF);
}

uint4 DecodeI4( uint2 st ) {
	uint sBlk = st.x >> 3;
	uint tBlk = st.y >> 3;
	uint widthBlks = ((dims_.x+7) >> 3);// + 1;
	uint base = (tBlk * widthBlks + sBlk) << 5;
	uint blkS = st.x & 7;
	uint blkT =  st.y & 7;
	uint blkOff = (blkT << 3) + blkS;

	uint rs = (blkOff & 1)?0:4;
	uint offset = base + (blkOff >> 1);

	uint v = ReadByte( offset );
	uint i = (blkOff&1) ? (v & 0x0F): ((v & 0xF0)>>4);
  i <<= 4;
	i |= (i>>4);
  return i.xxxx;
}
uint4 DecodeC4( uint2 st ) {
	uint sBlk = st.x >> 3;
	uint tBlk = st.y >> 3;
	uint widthBlks = ((dims_.x+7) >> 3);// + 1;
	uint base = (tBlk * widthBlks + sBlk) << 5;
	uint blkS = st.x & 7;
	uint blkT =  st.y & 7;
	uint blkOff = (blkT << 3) + blkS;

	uint rs = (blkOff & 1)?0:4;
	uint offset = base + (blkOff >> 1);

	uint v = ReadByte( offset );
	uint i = (blkOff&1) ? (v & 0x0F): ((v & 0xF0)>>4);

	return LUT_FUNC(i);
  //
}

uint4 Decode565( uint2 st ) {
	uint v = Decode2Bytes( st );
	return Read565(v);
}

uint4 DecodeIA8( uint2 st ) {
	uint v = Decode2Bytes( st );
	uint i = (v&0xFF);
	uint a = (v&0xFF00)>>8;
  return uint4(i.xxx,a);
}

uint4 NotImplemented( uint2 st ) {
	return uint4(uint(255).xx*st/dims_, 0,128 );
}

uint4 DecodeIA4( uint2 st ) {
  uint v = Decode1Byte( st );
	uint i = (v&0x0F)<<4; i |= (i>>4);
	uint a = (v&0xF0); a |= (a>>4);
  return uint4(i.xxx,a);
}



uint4 Decode5A3( uint2 st ) {
	uint v = Decode2Bytes( st );
	return Read5A3(v);
}

uint4 DecodeCmpr( uint2 st ) {
	uint pitch = ((dims_.x+7)/8)*32;
	uint tile = (st.y/8) * pitch + 32 * (st.x/8);

  uint2 pix = st & 0x7;
	uint offs = 8 * ( pix.x/4 ) + 16 * (pix.y/4);
  uint col0 = ReadTwoBytes( tile + offs + 0 );
  uint col1 = ReadTwoBytes( tile + offs + 2 );
  uint lut = ReadFourBytes( tile + offs + 4 );

  uint2 px = st & 0x3;
  uint idx = ( lut >> ( 32 - (px.x*2+2) - px.y*8 ) ) & 3;

	uint3 c0,c1;
	c0 = Read565(col0).rgb;
	c1 = Read565(col1).rgb;

	if ( col0<=col1 ) {
		// transparent case
		uint3 result = (idx&1) ? c1 : c0;
		if( idx == 2 ) {
			result = (c0+c1)>>1;
		}
		return uint4(result,idx==3 ? 0 : 255);
	} else {
		uint3 result = (idx&1) ? c1 : c0;
		if ( idx&2 ) {
			int3 delta = c1-c0;
			int3 tier = (delta>>1) - (delta >> 3);
			result = result + ( (idx&1) ? -tier : tier );
		}
		return uint4(result,255);
	}
}


uint4 DecodeRGBA8( uint2 st ) {
	uint pitch = ((dims_.x+3)/4)*64;
	uint tile = (st.y/4) * pitch + 64 * (st.x/4);
  uint offs = (st.x&0x3)*2 + (st.y&0x3)*8;

	uint ar = ReadTwoBytes( tile + offs );
	uint gb = ReadTwoBytes( tile + offs + 32);

	return uint4( (ar&0x00ff)>>0, (gb&0xff00)>>8, (gb&0x00ff)>>0, (ar&0xff00)>>8);
}

uint4 DecodeRGBA8FromTMEM( uint2 st ) {
	uint bgoffs = 2 * ((dims_.x+3)&~3) * ((dims_.y+3)&~3);
	uint pitch = ((dims_.x+3)/4)*32;
	uint tile = (st.y/4) * pitch + 32 * (st.x/4);
  uint offs = (st.x&0x3)*2 + (st.y&0x3)*8;

	uint ar = ReadTwoBytes( tile + offs );
	uint gb = ReadTwoBytes( tile + offs + bgoffs);

	return uint4( (ar&0x00ff)>>0, (gb&0xff00)>>8, (gb&0x00ff)>>0, (ar&0xff00)>>8);
}

uint4 DepalettizeC4( uint2 st ) {
	uint idx = uint(round(sourceData_.Load(int3(st.xy, 0)).r * 15.0));
	return LUT_FUNC(idx);
}

uint4 DepalettizeC8( uint2 st ) {
	uint idx = uint(round(sourceData_.Load(int3(st.xy, 0)).r * 255.0));
	return LUT_FUNC(idx);	
}

[numthreads(8,8,1)]
void main(in uint3 groupIdx : SV_GroupID, in uint3 subgroup : SV_GroupThreadID) {
	uint2 st = groupIdx.xy * 8 + subgroup.xy;
	if ( all( st < dims_) ) 
	{
		dstTexture_[st] = DECODER_FUNC( st ); 
	}
}
//
)HLSL";

static const char DEPALETTIZE_SHADER[] = R"HLSL(
//

#ifndef NUM_COLORS
#error NUM_COLORS was not defined
#endif

#ifndef LUT_FUNC
#error LUT_FUNC not set
#endif

Texture2D<float4> sourceData_ : register(t0);

Buffer<uint> lutData_ : register(t1);

float4 Read5A3( uint v ) {
	if (v&0x8000) {
		uint r = (v&0x7C00)>>7; r |= r>>5;
		uint g = (v&0x03E0)>>2;	g |= g>>5;
		uint b = (v&0x001F)<<3;	b |= b>>5;
		return float4(r,g,b,255)/ 255.0;
	} else {
		uint a = (v&0x7000)>>7; a |= (a>>3) | (a>>6);
		uint r = (v&0x0F00)>>4; r |= r>>4;
		uint g = (v&0x00F0)>>0; g |= g>>4;
		uint b = (v&0x000F)<<4; b |= b>>4;
		return float4(r,g,b,a) / 255.0;
	}
}

float4 Read565( uint v ) {
	uint b = (v&0x1F)<<3; b |= b >> 5;
	uint g = (v&0x7E0)>>3; g |= g >> 6;
	uint r = (v&0xF800)>>8; r |= r >> 5;
	return float4(r,g,b,255)/255.0;
}


float4 ReadLutIA8( uint idx ) {
	uint v = lutData_[idx];
	//v = ((v&0xff)<<8)| ((v&0xff00)>>8); // findout why byteswap is not needed in IA8 tlut reads
	return float4( uint(v&0xff).xxx, (v&0xff00)>>8 ) / 255.0;
}

float4 ReadLut565( uint idx ) {
	uint v = lutData_[idx];
	v = ((v&0xff)<<8)| ((v&0xff00)>>8);
  return Read565(v);
}

float4 ReadLut5A3( uint idx ) {
	uint v = lutData_[idx];
	v = ((v&0xff)<<8)| ((v&0xff00)>>8);
	return Read5A3(v);
}

void main(out float4 ocol0 : SV_Target, in float4 pos : SV_Position)
{	
#if NUM_COLORS == 16
	uint idx = uint(round(sourceData_.Load(int3(pos.xy, 0)).r * 15.0));
#else
	uint idx = uint(round(sourceData_.Load(int3(pos.xy, 0)).r * 255.0));
#endif
	ocol0 = LUT_FUNC(idx);
}
//
)HLSL";

void CSTextureDecoder::Init()
{
	m_pool_idx = 0;
	m_Pool_size = 0;
	m_ready = false;

	auto rawBd = CD3D11_BUFFER_DESC(1024*1024*4,D3D11_BIND_SHADER_RESOURCE);	
	HRESULT hr = D3D::device->CreateBuffer(&rawBd, nullptr, ToAddr(m_rawDataRsc));
	CHECK(SUCCEEDED(hr), "create texture decoder input buffer");
	D3D::SetDebugObjectName(m_rawDataRsc.get(), "texture decoder input buffer");
	auto outUavDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(m_rawDataRsc.get(), DXGI_FORMAT_R32_UINT, 0, (rawBd.ByteWidth) / 4, 0);
	hr = D3D::device->CreateShaderResourceView(m_rawDataRsc.get(),&outUavDesc,ToAddr(m_rawDataSrv));
	CHECK(SUCCEEDED(hr), "create texture decoder input buffer srv");
	D3D::SetDebugObjectName(m_rawDataSrv.get(), "texture decoder input buffer srv");

	u32 lutMaxEntries = (1 << 15) - 1;
	auto lutBd = CD3D11_BUFFER_DESC(sizeof(u16)*lutMaxEntries, D3D11_BIND_SHADER_RESOURCE);
	hr = D3D::device->CreateBuffer(&lutBd, nullptr, ToAddr(m_lutRsc));
	CHECK(SUCCEEDED(hr), "create texture decoder lut buffer");
	D3D::SetDebugObjectName(m_lutRsc.get(), "texture decoder lut buffer");
	auto outlutUavDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(m_lutRsc.get(), DXGI_FORMAT_R16_UINT, 0, lutMaxEntries, 0);
	hr = D3D::device->CreateShaderResourceView(m_lutRsc.get(), &outlutUavDesc, ToAddr(m_lutSrv));
	CHECK(SUCCEEDED(hr), "create texture decoder lut srv");
	D3D::SetDebugObjectName(m_lutSrv.get(), "texture decoder lut srv");

	auto paramBd = CD3D11_BUFFER_DESC(sizeof(u32) * 4, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC,
		D3D11_CPU_ACCESS_WRITE);
	hr = D3D::device->CreateBuffer(&paramBd, nullptr, ToAddr(m_params));
	CHECK(SUCCEEDED(hr), "create texture decoder params buffer");
	D3D::SetDebugObjectName(m_params.get(), "texture decoder params buffer");

	m_ready = true;

	// Warm up with shader cache
	char cache_filename[MAX_PATH];
	sprintf(cache_filename, "%sdx11-CSDECODER-cs.cache", File::GetUserPath(D_SHADERCACHE_IDX).c_str());
	m_shaderCache.OpenAndRead(cache_filename, ShaderCacheInserter(*this));
}

void CSTextureDecoder::Shutdown()
{
	m_pool.clear();
	m_rawDataSrv.reset();
	m_rawDataRsc.reset();
	m_lutSrv.reset();
	m_lutRsc.reset();
	m_params.reset();
	for (size_t i = 0; i < 3; i++)
	{
		for (size_t j = 0; j < 2; j++)
		{
			m_depalettize_shaders[i][j].reset();
		}
	}
	m_ready = false;
}

const bool DecFuncSupported[] = {
	true,//GX_TF_I4     = 0x0,
	true,//GX_TF_I8     = 0x1,
	true,//GX_TF_IA4    = 0x2,
	true,//GX_TF_IA8    = 0x3,
	true,//GX_TF_RGB565 = 0x4,
	true,//GX_TF_RGB5A3 = 0x5,
	true,//GX_TF_RGBA8  = 0x6,
	false,//
	true,//GX_TF_C4     = 0x8,
	true,//GX_TF_C8     = 0x9,
	true,//GX_TF_C14X2  = 0xA,
	false,//
	false,//
	false,//
	false,//GX_TF_CMPR   = 0xE,
	true,//
};

bool CSTextureDecoder::FormatSupported(u32 srcFmt)
{
	return DecFuncSupported[u32(srcFmt) & 0xF];
}

char const* DecFunc[] = {
"DecodeI4",//GX_TF_I4     = 0x0,
"DecodeI8",//GX_TF_I8     = 0x1,
"DecodeIA4",//GX_TF_IA4    = 0x2,
"DecodeIA8",//GX_TF_IA8    = 0x3,
"Decode565",//GX_TF_RGB565 = 0x4,
"Decode5A3",//GX_TF_RGB5A3 = 0x5,
"DecodeRGBA8",//GX_TF_RGBA8  = 0x6,
"NotImplemented",//
"DecodeC4",//GX_TF_C4     = 0x8,
"DecodeC8",//GX_TF_C8     = 0x9,
"DecodeC14",//GX_TF_C14X2  = 0xA,
"NotImplemented",//
"NotImplemented",//
"NotImplemented",//
"DecodeCmpr",//GX_TF_CMPR   = 0xE,
"DecodeRGBA8FromTMEM",//
};

char const* DepalettizeFuncs[] = {
"DepalettizeC4",
"DepalettizeC8"
};

char const* LutFunc[] = {
"ReadLutIA8",
"ReadLut565",
"ReadLut5A3",
};

bool CSTextureDecoder::SetStaticShader(u32 srcFmt, u32 lutFmt) 
{
	if (!DecFuncSupported[srcFmt & 0xF])
	{
		return false;
	}
	u32 rawFmt = (srcFmt & 0xF);
	if (rawFmt < GX_TF_C4 || rawFmt > GX_TF_C14X2)
	{
		lutFmt = 0;
	}
	auto key = MakeComboKey(srcFmt,lutFmt);

	auto it = m_staticShaders.find(key);

	if (it!=m_staticShaders.end()) 
	{
		D3D::context->CSSetShader(it->second.get(),nullptr, 0);
		return bool(it->second);
	}

	// Shader permutation not found, so compile it
	
	D3D_SHADER_MACRO macros[] = 
	{
		{ "DECODER_FUNC", DecFunc[rawFmt] },
		{ "LUT_FUNC", LutFunc[lutFmt & 0xF] },		
		{ nullptr, nullptr }
	};

	D3DBlob bytecode = nullptr;
	if (!D3D::CompileShader(D3D::ShaderType::Compute, DECODER_CS, bytecode, macros)) {
		WARN_LOG(VIDEO, "noooo");
	}

	m_shaderCache.Append(key, bytecode.Data(), u32(bytecode.Size()));

	auto & result = m_staticShaders[key];
	HRESULT hr = D3D::device->CreateComputeShader(bytecode.Data(), bytecode.Size(), nullptr, ToAddr(result) );
	CHECK(SUCCEEDED(hr), "create efb encoder pixel shader");
	D3D::context->CSSetShader(result.get(),nullptr, 0);

	return bool(result);
}

ID3D11PixelShader* CSTextureDecoder::GetDepalettizerPShader(BaseType srcFmt, u32 lutFmt)
{
	switch (srcFmt)
	{

	case Unorm4:
		if (!m_depalettize_shaders[lutFmt][Unorm4])
		{
			D3D_SHADER_MACRO macros[] = {
				{ "NUM_COLORS", "16" },
				{ "LUT_FUNC", LutFunc[lutFmt & 0xF] },
				{ NULL, NULL }
			};
			m_depalettize_shaders[lutFmt][Unorm4] = D3D::CompileAndCreatePixelShader(DEPALETTIZE_SHADER, macros);
		}
		return m_depalettize_shaders[lutFmt][Unorm4].get();

	case Unorm8:
		if (!m_depalettize_shaders[lutFmt][Unorm8])
		{
			D3D_SHADER_MACRO macros[] = {
				{ "NUM_COLORS", "256" },
				{ "LUT_FUNC", LutFunc[lutFmt & 3] },
				{ NULL, NULL }
			};
			m_depalettize_shaders[lutFmt][Unorm8] = D3D::CompileAndCreatePixelShader(DEPALETTIZE_SHADER, macros);
		}
		return m_depalettize_shaders[lutFmt][Unorm8].get();	
	default:
		_assert_msg_(VIDEO, 0, "Invalid depalettizer base type");
		return NULL;
	}
}

bool CSTextureDecoder::SetDepalettizeShader(BaseType srcFmt, u32 lutFmt)
{
	u32 rawFmt = u32(srcFmt) + 16;
	auto key = MakeComboKey(rawFmt, lutFmt);

	auto it = m_staticShaders.find(key);

	if (it != m_staticShaders.end())
	{
		D3D::context->CSSetShader(it->second.get(), nullptr, 0);
		return bool(it->second);
	}

	// Shader permutation not found, so compile it

	D3D_SHADER_MACRO macros[] =
	{
		{ "DECODER_FUNC", DepalettizeFuncs[u32(srcFmt) & 1] },
		{ "LUT_FUNC", LutFunc[lutFmt & 3] },
		{ nullptr, nullptr }
	};

	D3DBlob bytecode = nullptr;
	if (!D3D::CompileShader(D3D::ShaderType::Compute, DECODER_CS, bytecode, macros)) {
		WARN_LOG(VIDEO, "noooo");
	}

	m_shaderCache.Append(key, bytecode.Data(), u32(bytecode.Size()));

	auto & result = m_staticShaders[key];
	HRESULT hr = D3D::device->CreateComputeShader(bytecode.Data(), bytecode.Size(), nullptr, ToAddr(result));
	CHECK(SUCCEEDED(hr), "create efb encoder pixel shader");
	D3D::context->CSSetShader(result.get(), nullptr, 0);

	return bool(result);
}

ID3D11ComputeShader* CSTextureDecoder::InsertShader( ComboKey const &key, u8 const *data, u32 sz) {
	auto & result = m_staticShaders[key];
	HRESULT hr = D3D::device->CreateComputeShader(data, sz, nullptr, ToAddr(result) );
	CHECK(SUCCEEDED(hr), "create efb encoder pixel shader");
	return result.get();
}

void CSTextureDecoder::LoadLut(u32 lutFmt, void* addr, u32 size ) 
{
	if (!m_ready)
	{
		return;
	}
	D3D11_BOX box{0,0,0,size,1,1};
	D3D::context->UpdateSubresource(m_lutRsc.get(),0,&box,addr,0,0);
	m_lutFmt = lutFmt;
}
bool CSTextureDecoder::Decode(const u8* src, u32 srcsize, u32 srcFmt, u32 w, u32 h, u32 level, D3DTexture2D& dstTexture)
{
	if (!m_ready || w < 32 || h < 32) // Make sure we initialized OK and texture size is big enough
		return false;

	if (!SetStaticShader(srcFmt,m_lutFmt)) 
	{
		return false;
	}

	if (m_pool_idx == m_pool.size())
	{
		if (m_pool.size() < MAX_POOL_SIZE)
		{
			// create the pool texture here
			auto desc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R8G8B8A8_UINT, 1024, 1024, 1, 1, D3D11_BIND_UNORDERED_ACCESS);

			HRESULT hr;
			PoolValue val;
			hr = D3D::device->CreateTexture2D(&desc, nullptr, ToAddr(val.m_rsc));
			CHECK(SUCCEEDED(hr), "create pool texture for texture decoder");

			hr = D3D::device->CreateUnorderedAccessView(val.m_rsc.get(), nullptr, ToAddr(val.m_uav));
			CHECK(SUCCEEDED(hr), "create pool UAV for texture decoder");
			m_pool.push_back(std::move(val));
		}
		else
		{
			m_pool_idx = m_pool_idx % m_pool.size();
		}
	}
	D3D11_BOX box{ 0, 0, 0, srcsize, 1, 1 };
	D3D::context->UpdateSubresource(m_rawDataRsc.get(), 0, &box, src, 0, 0);
	ID3D11UnorderedAccessView* uav = m_pool[m_pool_idx].m_uav.get();
	D3D::context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	ID3D11ShaderResourceView* srvs[] = { m_rawDataSrv.get(), m_lutSrv.get() };	
	D3D::context->CSSetShaderResources(0, 2, srvs);
	D3D11_MAPPED_SUBRESOURCE map;
	D3D::context->Map(m_params.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	((u32*)map.pData)[0] = w;
	((u32*)map.pData)[1] = h;
	D3D::context->Unmap(m_params.get(), 0);
	D3D::context->CSSetConstantBuffers(0, 1, D3D::ToAddr(m_params));

	D3D::context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);

	uav = nullptr;
	D3D::context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	D3D11_BOX pSrcBox;
	pSrcBox.left = 0;
	pSrcBox.top = 0;
	pSrcBox.front = 0;
	pSrcBox.right = w;
	pSrcBox.bottom = h;
	pSrcBox.back = 1;
	D3D::context->CopySubresourceRegion(dstTexture.GetTex(), level, 0, 0, 0, m_pool[m_pool_idx].m_rsc.get(), 0, &pSrcBox);
	m_pool_idx++;
	return true;
}

bool CSTextureDecoder::DecodeRGBAFromTMEM( u8 const * ar_src, u8 const * bg_src, u32 w, u32 h, D3DTexture2D& dstTexture) 
{

	if (!m_ready) // Make sure we initialized OK
		return false;

	if (!SetStaticShader(0xf,0)) 
	{
		return false;
	}

	if (m_pool_idx == m_pool.size())
	{
		if (m_pool.size() < MAX_POOL_SIZE)
		{
			// create the pool texture here
			auto desc = CD3D11_TEXTURE2D_DESC(DXGI_FORMAT_R8G8B8A8_UINT, 1024, 1024, 1, 1, D3D11_BIND_UNORDERED_ACCESS);

			HRESULT hr;
			PoolValue val;
			hr = D3D::device->CreateTexture2D(&desc, nullptr, ToAddr(val.m_rsc));
			CHECK(SUCCEEDED(hr), "create pool texture for texture decoder");

			hr = D3D::device->CreateUnorderedAccessView(val.m_rsc.get(), nullptr, ToAddr(val.m_uav));
			CHECK(SUCCEEDED(hr), "create pool UAV for texture decoder");
			m_pool.push_back(std::move(val));
		}
		else
		{
			m_pool_idx = m_pool_idx % m_pool.size();
		}
	}

	u32 aw = (w + 4)&~4;
	u32 ah = (h + 4)&~4;

	D3D11_BOX box{ 0, 0, 0, (aw*ah) << 1, 1, 1 };
	D3D::context->UpdateSubresource(m_rawDataRsc.get(), 0, &box, ar_src, 0, 0);

	D3D11_BOX box2{ (aw*ah) << 1, 0, 0, 2 * ((aw*ah) << 1), 1, 1 };
	D3D::context->UpdateSubresource(m_rawDataRsc.get(), 0, &box2, bg_src, 0, 0);

	ID3D11UnorderedAccessView* uav = m_pool[m_pool_idx].m_uav.get();
	D3D::context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	ID3D11ShaderResourceView* srvs[] = { m_rawDataSrv.get() };
	D3D::context->CSSetShaderResources(0, 1, srvs);
	D3D11_MAPPED_SUBRESOURCE map;
	D3D::context->Map(m_params.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	((u32*)map.pData)[0] = w;
	((u32*)map.pData)[1] = h;
	D3D::context->Unmap(m_params.get(), 0);
	D3D::context->CSSetConstantBuffers(0, 1, D3D::ToAddr(m_params));

	D3D::context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);

	uav = nullptr;
	D3D::context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	D3D11_BOX pSrcBox;
	pSrcBox.left = 0;
	pSrcBox.top = 0;
	pSrcBox.front = 0;
	pSrcBox.right = w;
	pSrcBox.bottom = h;
	pSrcBox.back = 1;
	D3D::context->CopySubresourceRegion(dstTexture.GetTex(), 0, 0, 0, 0, m_pool[m_pool_idx].m_rsc.get(), 0, &pSrcBox);
	m_pool_idx++;
	return true;
}

bool CSTextureDecoder::Depalettize(D3DTexture2D& dstTexture, D3DTexture2D& srcTexture, BaseType baseType, u32 width, u32 height)
{
	if (true/*D3D::GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0*/) // Disable compute shader path, is not working
	{
		ID3D11PixelShader* shader = GetDepalettizerPShader(baseType, m_lutFmt);
		if (!shader)
		{
			return false;
		}

		g_renderer->ResetAPIState();
		D3D::stateman->Apply();

		D3D11_VIEWPORT vp = CD3D11_VIEWPORT(0.f, 0.f, FLOAT(width), FLOAT(height));
		D3D::context->RSSetViewports(1, &vp);

		D3D::context->OMSetRenderTargets(1, &dstTexture.GetRTV(), NULL);
		D3D::context->PSSetShaderResources(1, 1, D3D::ToAddr(m_lutSrv));
		D3D11_RECT rsource = { 0, 0, width, height };
		D3D::drawShadedTexQuad(
			srcTexture.GetSRV(),
			&rsource,
			width, height,
			shader, 
			VertexShaderCache::GetSimpleVertexShader(),
			VertexShaderCache::GetSimpleInputLayout()
			);

		ID3D11ShaderResourceView* nullDummy = NULL;
		D3D::context->PSSetShaderResources(0, 1, &nullDummy);
		D3D::context->PSSetShaderResources(1, 1, &nullDummy);

		g_renderer->RestoreAPIState();
		D3D::context->OMSetRenderTargets(1,
			&FramebufferManager::GetEFBColorTexture()->GetRTV(),
			FramebufferManager::GetEFBDepthTexture()->GetDSV());
	}
	else
	{
		if (!SetDepalettizeShader(baseType, m_lutFmt))
		{
			return false;
		}
		ID3D11UnorderedAccessView* uav = dstTexture.GetUAV();
		D3D::context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

		ID3D11ShaderResourceView* srvs[] = { nullptr, m_lutSrv.get(), srcTexture.GetSRV() };
		D3D::context->CSSetShaderResources(0, 3, srvs);

		D3D11_MAPPED_SUBRESOURCE map;
		D3D::context->Map(m_params.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
		((u32*)map.pData)[0] = width;
		((u32*)map.pData)[1] = height;
		D3D::context->Unmap(m_params.get(), 0);
		D3D::context->CSSetConstantBuffers(0, 1, D3D::ToAddr(m_params));

		D3D::context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
		uav = nullptr;
		D3D::context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		ID3D11ShaderResourceView* srv = nullptr;
		D3D::context->CSSetShaderResources(2, 1, &srv);
	}
	return true;
}

}
