// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2004-2016 Christoph Oelckers
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//

#include "w_wad.h"
#include "m_png.h"
#include "sbar.h"
#include "stats.h"
#include "r_utility.h"
#include "c_dispatch.h"
#include "hw_ihwtexture.h"
#include "hw_material.h"

EXTERN_CVAR(Bool, gl_texture_usehires)

//===========================================================================
//
//
//
//===========================================================================

float FTexCoordInfo::RowOffset(float rowoffset) const
{
	float tscale = fabs(mTempScale.Y);
	float scale = fabs(mScale.Y);

	if (tscale == 1.f)
	{
		if (scale == 1.f || mWorldPanning) return rowoffset;
		else return rowoffset / scale;
	}
	else
	{
		if (mWorldPanning) return rowoffset / tscale;
		else return rowoffset / scale;
	}
}

//===========================================================================
//
//
//
//===========================================================================

float FTexCoordInfo::TextureOffset(float textureoffset) const
{
	float tscale = fabs(mTempScale.X);
	float scale = fabs(mScale.X);
	if (tscale == 1.f)
	{
		if (scale == 1.f || mWorldPanning) return textureoffset;
		else return textureoffset / scale;
	}
	else
	{
		if (mWorldPanning) return textureoffset / tscale;
		else return textureoffset / scale;
	}
}

//===========================================================================
//
// Returns the size for which texture offset coordinates are used.
//
//===========================================================================

float FTexCoordInfo::TextureAdjustWidth() const
{
	if (mWorldPanning) 
	{
		float tscale = fabs(mTempScale.X);
		if (tscale == 1.f) return (float)mRenderWidth;
		else return mWidth / fabs(tscale);
	}
	else return (float)mWidth;
}



//===========================================================================
//
//
//
//===========================================================================
IHardwareTexture * FMaterial::ValidateSysTexture(FTexture * tex, bool expand)
{
	if (tex	&& tex->UseType!=ETextureType::Null)
	{
		IHardwareTexture *gltex = tex->SystemTexture[expand];
		if (gltex == nullptr) 
		{
			gltex = tex->SystemTexture[expand] = screen->CreateHardwareTexture(tex);
		}
		return gltex;
	}
	return nullptr;
}

//===========================================================================
//
// Constructor
//
//===========================================================================
TArray<FMaterial *> FMaterial::mMaterials;
int FMaterial::mMaxBound;

FMaterial::FMaterial(FTexture * tx, bool expanded)
{
	mShaderIndex = SHADER_Default;
	sourcetex = tex = tx;

	// TODO: apply custom shader object here
	/* if (tx->CustomShaderDefinition)
	{
	}
	else
	*/
	if (tx->UseType == ETextureType::SWCanvas && tx->WidthBits == 0)
	{
		mShaderIndex = SHADER_Paletted;
	}
	else if (tx->bWarped)
	{
		mShaderIndex = tx->bWarped; // This picks SHADER_Warp1 or SHADER_Warp2
		tx->shaderspeed = static_cast<FWarpTexture*>(tx)->GetSpeed();
	}
	else if (tx->bHasCanvas)
	{
		if (tx->shaderindex >= FIRST_USER_SHADER)
		{
			mShaderIndex = tx->shaderindex;
		}
		// no brightmap for cameratexture
	}
	else
	{
		if (tx->shaderindex >= FIRST_USER_SHADER)
		{
			for (auto &texture : tx->CustomShaderTextures)
			{
				if(texture == nullptr) continue;
				ValidateSysTexture(texture, expanded);
				mTextureLayers.Push(texture);
			}
			mShaderIndex = tx->shaderindex;
		}
		else
		{
			if (tx->Normal && tx->Specular)
			{
				for (auto &texture : { tx->Normal, tx->Specular })
				{
					ValidateSysTexture(texture, expanded);
					mTextureLayers.Push(texture);
				}
				mShaderIndex = SHADER_Specular;
			}
			else if (tx->Normal && tx->Metallic && tx->Roughness && tx->AmbientOcclusion)
			{
				for (auto &texture : { tx->Normal, tx->Metallic, tx->Roughness, tx->AmbientOcclusion })
				{
					ValidateSysTexture(texture, expanded);
					mTextureLayers.Push(texture);
				}
				mShaderIndex = SHADER_PBR;
			}

			tx->CreateDefaultBrightmap();
			if (tx->Brightmap != NULL)
			{
				ValidateSysTexture(tx->Brightmap, expanded);
				mTextureLayers.Push(tx->Brightmap);
				if (mShaderIndex == SHADER_Specular)
					mShaderIndex = SHADER_SpecularBrightmap;
				else if (mShaderIndex == SHADER_PBR)
					mShaderIndex = SHADER_PBRBrightmap;
				else
					mShaderIndex = SHADER_Brightmap;
			}
		}
	}
	mBaseLayer = ValidateSysTexture(tx, expanded);


	mWidth = tx->GetWidth();
	mHeight = tx->GetHeight();
	mLeftOffset = tx->GetLeftOffset(0);	// These only get used by decals and decals should not use renderer-specific offsets.
	mTopOffset = tx->GetTopOffset(0);
	mRenderWidth = tx->GetScaledWidth();
	mRenderHeight = tx->GetScaledHeight();
	mSpriteU[0] = mSpriteV[0] = 0.f;
	mSpriteU[1] = mSpriteV[1] = 1.f;

	FTexture *basetex = tx->GetRedirect();
	// allow the redirect only if the texture is not expanded or the scale matches.
	if (!expanded || (tx->Scale.X == basetex->Scale.X && tx->Scale.Y == basetex->Scale.Y))
	{
		sourcetex = basetex;
		mBaseLayer = ValidateSysTexture(basetex, expanded);
	}

	mExpanded = expanded;
	if (expanded)
	{
		int oldwidth = mWidth;
		int oldheight = mHeight;

		mTrimResult = TrimBorders(trim);	// get the trim size before adding the empty frame
		mWidth += 2;
		mHeight += 2;
		mRenderWidth = mRenderWidth * mWidth / oldwidth;
		mRenderHeight = mRenderHeight * mHeight / oldheight;

	}
	SetSpriteRect();

	mTextureLayers.ShrinkToFit();
	mMaxBound = -1;
	mMaterials.Push(this);
	tx->Material[expanded] = this;
	if (tx->bHasCanvas) tx->bTranslucent = 0;
}

//===========================================================================
//
// Destructor
//
//===========================================================================

FMaterial::~FMaterial()
{
	for(unsigned i=0;i<mMaterials.Size();i++)
	{
		if (mMaterials[i]==this) 
		{
			mMaterials.Delete(i);
			break;
		}
	}

}

//===========================================================================
//
// Set the sprite rectangle
//
//===========================================================================

void FMaterial::SetSpriteRect()
{
	auto leftOffset = tex->GetLeftOffsetHW();
	auto topOffset = tex->GetTopOffsetHW();

	float fxScale = (float)tex->Scale.X;
	float fyScale = (float)tex->Scale.Y;

	// mSpriteRect is for positioning the sprite in the scene.
	mSpriteRect.left = -leftOffset / fxScale;
	mSpriteRect.top = -topOffset / fyScale;
	mSpriteRect.width = mWidth / fxScale;
	mSpriteRect.height = mHeight / fyScale;

	if (mExpanded)
	{
		// a little adjustment to make sprites look better with texture filtering:
		// create a 1 pixel wide empty frame around them.

		int oldwidth = mWidth - 2;
		int oldheight = mHeight - 2;

		leftOffset += 1;
		topOffset += 1;

		// Reposition the sprite with the frame considered
		mSpriteRect.left = -leftOffset / fxScale;
		mSpriteRect.top = -topOffset / fyScale;
		mSpriteRect.width = mWidth / fxScale;
		mSpriteRect.height = mHeight / fyScale;

		if (mTrimResult)
		{
			mSpriteRect.left += trim[0] / fxScale;
			mSpriteRect.top += trim[1] / fyScale;

			mSpriteRect.width -= (oldwidth - trim[2]) / fxScale;
			mSpriteRect.height -= (oldheight - trim[3]) / fyScale;

			mSpriteU[0] = trim[0] / (float)mWidth;
			mSpriteV[0] = trim[1] / (float)mHeight;
			mSpriteU[1] -= (oldwidth - trim[0] - trim[2]) / (float)mWidth;
			mSpriteV[1] -= (oldheight - trim[1] - trim[3]) / (float)mHeight;
		}
	}
}


//===========================================================================
// 
//  Finds empty space around the texture. 
//  Used for sprites that got placed into a huge empty frame.
//
//===========================================================================

bool FMaterial::TrimBorders(uint16_t *rect)
{
	PalEntry col;
	int w;
	int h;

	unsigned char *buffer = sourcetex->CreateTexBuffer(0, w, h);

	if (buffer == NULL) 
	{
		return false;
	}
	if (w != mWidth || h != mHeight)
	{
		// external Hires replacements cannot be trimmed.
		delete [] buffer;
		return false;
	}

	int size = w*h;
	if (size == 1)
	{
		// nothing to be done here.
		rect[0] = 0;
		rect[1] = 0;
		rect[2] = 1;
		rect[3] = 1;
		delete[] buffer;
		return true;
	}
	int first, last;

	for(first = 0; first < size; first++)
	{
		if (buffer[first*4+3] != 0) break;
	}
	if (first >= size)
	{
		// completely empty
		rect[0] = 0;
		rect[1] = 0;
		rect[2] = 1;
		rect[3] = 1;
		delete [] buffer;
		return true;
	}

	for(last = size-1; last >= first; last--)
	{
		if (buffer[last*4+3] != 0) break;
	}

	rect[1] = first / w;
	rect[3] = 1 + last/w - rect[1];

	rect[0] = 0;
	rect[2] = w;

	unsigned char *bufferoff = buffer + (rect[1] * w * 4);
	h = rect[3];

	for(int x = 0; x < w; x++)
	{
		for(int y = 0; y < h; y++)
		{
			if (bufferoff[(x+y*w)*4+3] != 0) goto outl;
		}
		rect[0]++;
	}
outl:
	rect[2] -= rect[0];

	for(int x = w-1; rect[2] > 1; x--)
	{
		for(int y = 0; y < h; y++)
		{
			if (bufferoff[(x+y*w)*4+3] != 0) 
			{
				delete [] buffer;
				return true;
			}
		}
		rect[2]--;
	}
	delete [] buffer;
	return true;
}

//===========================================================================
//
//
//
//===========================================================================
void FMaterial::Precache()
{
	screen->PrecacheMaterial(this, 0);
}

//===========================================================================
//
//
//
//===========================================================================
void FMaterial::PrecacheList(SpriteHits &translations)
{
	if (mBaseLayer != nullptr) mBaseLayer->CleanUnused(translations);
	SpriteHits::Iterator it(translations);
	SpriteHits::Pair *pair;
	while(it.NextPair(pair)) screen->PrecacheMaterial(this, pair->Key);
}

//===========================================================================
//
// Retrieve texture coordinate info for per-wall scaling
//
//===========================================================================

void FMaterial::GetTexCoordInfo(FTexCoordInfo *tci, float x, float y) const
{
	if (x == 1.f)
	{
		tci->mRenderWidth = mRenderWidth;
		tci->mScale.X = (float)tex->Scale.X;
		tci->mTempScale.X = 1.f;
	}
	else
	{
		float scale_x = x * (float)tex->Scale.X;
		tci->mRenderWidth = xs_CeilToInt(mWidth / scale_x);
		tci->mScale.X = scale_x;
		tci->mTempScale.X = x;
	}

	if (y == 1.f)
	{
		tci->mRenderHeight = mRenderHeight;
		tci->mScale.Y = (float)tex->Scale.Y;
		tci->mTempScale.Y = 1.f;
	}
	else
	{
		float scale_y = y * (float)tex->Scale.Y;
		tci->mRenderHeight = xs_CeilToInt(mHeight / scale_y);
		tci->mScale.Y = scale_y;
		tci->mTempScale.Y = y;
	}
	if (tex->bHasCanvas) 
	{
		tci->mScale.Y = -tci->mScale.Y;
		tci->mRenderHeight = -tci->mRenderHeight;
	}
	tci->mWorldPanning = tex->bWorldPanning;
	tci->mWidth = mWidth;
}

//===========================================================================
//
//
//
//===========================================================================

int FMaterial::GetAreas(FloatRect **pAreas) const
{
	if (mShaderIndex == SHADER_Default)	// texture splitting can only be done if there's no attached effects
	{
		*pAreas = sourcetex->areas;
		return sourcetex->areacount;
	}
	else
	{
		return 0;
	}
}

//==========================================================================
//
// Gets a texture from the texture manager and checks its validity for
// GL rendering. 
//
//==========================================================================

FMaterial * FMaterial::ValidateTexture(FTexture * tex, bool expand)
{
again:
	if (tex	&& tex->UseType!=ETextureType::Null)
	{
		if (tex->bNoExpand) expand = false;

		FMaterial *gltex = tex->Material[expand];
		if (gltex == NULL) 
		{
			if (expand)
			{
				if (tex->bWarped || tex->bHasCanvas || tex->shaderindex >= FIRST_USER_SHADER || (tex->shaderindex >= SHADER_Specular && tex->shaderindex <= SHADER_PBRBrightmap))
				{
					tex->bNoExpand = true;
					goto again;
				}
				if (tex->Brightmap != NULL &&
					(tex->GetWidth() != tex->Brightmap->GetWidth() ||
					tex->GetHeight() != tex->Brightmap->GetHeight())
					)
				{
					// do not expand if the brightmap's size differs.
					tex->bNoExpand = true;
					goto again;
				}
			}
			gltex = new FMaterial(tex, expand);
		}
		return gltex;
	}
	return NULL;
}

FMaterial * FMaterial::ValidateTexture(FTextureID no, bool expand, bool translate)
{
	return ValidateTexture(translate? TexMan(no) : TexMan[no], expand);
}


//==========================================================================
//
// Flushes all hardware dependent data
//
//==========================================================================

void FMaterial::FlushAll()
{
	for(int i=mMaterials.Size()-1;i>=0;i--)
	{
		mMaterials[i]->mBaseLayer->Clean(true);
	}
	// This is for shader layers. All shader layers must be managed by the texture manager
	// so this will catch everything.
	for(int i=TexMan.NumTextures()-1;i>=0;i--)
	{
		for (int j = 0; j < 2; j++)
		{
			auto gltex = TexMan.ByIndex(i)->SystemTexture[j];
			if (gltex != nullptr) gltex->Clean(true);
		}
	}
}

void FMaterial::Clean(bool f)
{
	// This somehow needs to deal with the other layers as well, but they probably need some form of reference counting to work properly...
	mBaseLayer->Clean(f);
}

//===========================================================================
// 
//	Quick'n dirty image rescaling.
//
// This will only be used when the source texture is larger than
// what the hardware can manage (extremely rare in Doom)
//
// Code taken from wxWidgets
//
//===========================================================================

struct BoxPrecalc
{
	int boxStart;
	int boxEnd;
};

static void ResampleBoxPrecalc(TArray<BoxPrecalc>& boxes, int oldDim)
{
	int newDim = boxes.Size();
	const double scale_factor_1 = double(oldDim) / newDim;
	const int scale_factor_2 = (int)(scale_factor_1 / 2);

	for (int dst = 0; dst < newDim; ++dst)
	{
		// Source pixel in the Y direction
		const int src_p = int(dst * scale_factor_1);

		BoxPrecalc& precalc = boxes[dst];
		precalc.boxStart = clamp<int>(int(src_p - scale_factor_1 / 2.0 + 1), 0, oldDim - 1);
		precalc.boxEnd = clamp<int>(MAX<int>(precalc.boxStart + 1, int(src_p + scale_factor_2)), 0, oldDim - 1);
	}
}

void ResizeTexture(int swidth, int sheight, int width, int height, unsigned char *src_data, unsigned char *dst_data)
{

	// This function implements a simple pre-blur/box averaging method for
	// downsampling that gives reasonably smooth results To scale the image
	// down we will need to gather a grid of pixels of the size of the scale
	// factor in each direction and then do an averaging of the pixels.

	TArray<BoxPrecalc> vPrecalcs(height, true);
	TArray<BoxPrecalc> hPrecalcs(width, true);

	ResampleBoxPrecalc(vPrecalcs, sheight);
	ResampleBoxPrecalc(hPrecalcs, swidth);

	int averaged_pixels, averaged_alpha, src_pixel_index;
	double sum_r, sum_g, sum_b, sum_a;

	for (int y = 0; y < height; y++)         // Destination image - Y direction
	{
		// Source pixel in the Y direction
		const BoxPrecalc& vPrecalc = vPrecalcs[y];

		for (int x = 0; x < width; x++)      // Destination image - X direction
		{
			// Source pixel in the X direction
			const BoxPrecalc& hPrecalc = hPrecalcs[x];

			// Box of pixels to average
			averaged_pixels = 0;
			averaged_alpha = 0;
			sum_r = sum_g = sum_b = sum_a = 0.0;

			for (int j = vPrecalc.boxStart; j <= vPrecalc.boxEnd; ++j)
			{
				for (int i = hPrecalc.boxStart; i <= hPrecalc.boxEnd; ++i)
				{
					// Calculate the actual index in our source pixels
					src_pixel_index = j * swidth + i;

					int a = src_data[src_pixel_index * 4 + 3];
					if (a > 0)	// do not use color from fully transparent pixels
					{
						sum_r += src_data[src_pixel_index * 4 + 0];
						sum_g += src_data[src_pixel_index * 4 + 1];
						sum_b += src_data[src_pixel_index * 4 + 2];
						sum_a += a;
						averaged_pixels++;
					}
					averaged_alpha++;

				}
			}

			// Calculate the average from the sum and number of averaged pixels
			dst_data[0] = (unsigned char)xs_CRoundToInt(sum_r / averaged_pixels);
			dst_data[1] = (unsigned char)xs_CRoundToInt(sum_g / averaged_pixels);
			dst_data[2] = (unsigned char)xs_CRoundToInt(sum_b / averaged_pixels);
			dst_data[3] = (unsigned char)xs_CRoundToInt(sum_a / averaged_alpha);
			dst_data += 4;
		}
	}
}

//==========================================================================
//
// Prints some texture info
//
//==========================================================================

CCMD(textureinfo)
{
	int cntt = 0;
	for (int i = 0; i < TexMan.NumTextures(); i++)
	{
		FTexture *tex = TexMan.ByIndex(i);
		if (tex->SystemTexture[0] || tex->SystemTexture[1] || tex->Material[0] || tex->Material[1])
		{
			int lump = tex->GetSourceLump();
			Printf(PRINT_LOG, "Texture '%s' (Index %d, Lump %d, Name '%s'):\n", tex->Name.GetChars(), i, lump, Wads.GetLumpFullName(lump));
			if (tex->Material[0])
			{
				Printf(PRINT_LOG, "in use (normal)\n");
			}
			else if (tex->SystemTexture[0])
			{
				Printf(PRINT_LOG, "referenced (normal)\n");
			}
			if (tex->Material[1])
			{
				Printf(PRINT_LOG, "in use (expanded)\n");
			}
			else if (tex->SystemTexture[1])
			{
				Printf(PRINT_LOG, "referenced (normal)\n");
			}
			cntt++;
		}
	}
	Printf(PRINT_LOG, "%d system textures\n", cntt);
}

