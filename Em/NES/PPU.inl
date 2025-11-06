static uint8_t const XIntersectLUT[] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};

struct NES_PPU
{
	NES_Cart_Base *cart = nullptr;

	uint8_t palette[2][4][4] = {0};
	struct Sprite
	{
		uint8_t x = 255, y = 255, tile = 0;
		uint8_t palette = 0;
		bool drawBehindBackground = true, flipH = true, flipV = true;
	} OAM[64];

	int scanline = 0;
	int scanlinePosition = 0;

	struct ScanlineSprite
	{
		uint8_t plane1, plane2;
		uint8_t index;
		uint8_t x;
	} cacheScanlineSprites[64];
	int cacheScanlineSpritesCount = 0;

	//uint8_t baseNametable = 0;
	bool bigVRamIncrementJump = false;
	bool useSecondTileTableBG = false;
	bool useSecondTileTableSprite = false;
	bool tallSprites = false;
	bool enableNMI = false;

	uint8_t OAMAccessAddress = 0;
	//uint16_t scrollX = 0, scrollY = 0;
	uint8_t scrollXFine = 0;
	uint8_t tempScrollXFine = 0;
	uint16_t VRamAccessAddress = 0;
	uint16_t tempVRamAccessAddress = 0;
	uint8_t delayVRead = 0;

	bool enableLeftBlankBG = false;
	bool enableLeftBlankSprite = false;
	bool enableBG = false;
	bool enableSprite = false;
	bool writeToggle = false;

	bool inVBlank = false;
	bool isEvenFrame = false;
	bool requestNMI = false;
	bool hasSpriteOverflow = false;
	bool sprite0Hit = false;

	Color screen[240][256];

	void SetV(uint16_t address, uint8_t value)
	{
		if(address < 0x3F00)
		{
			if(cart != nullptr)
				cart->SetV(address, value, false);
		}
		else if(0x3F00 <= address && address < 0x3FFF)
		{
			address %= 0x20;
			uint8_t *p = reinterpret_cast<uint8_t*>(palette);
			p[address] = value;
			
			if(address == 0x00)
				p[0x10] = value;
			else if(address == 0x10)
				p[0x00] = value;
		}
	}

	void GetV(uint16_t address, uint8_t &value)
	{
		if(address < 0x3F00)
		{
			if(cart != nullptr)
				cart->GetV(address, value, false);
		}
		else if(0x3F00 <= address && address < 0x3FFF)
		{
			address %= 0x20;
			uint8_t *p = reinterpret_cast<uint8_t*>(palette);
			value = p[address];
		}
	}

	void Set(uint16_t address, uint8_t value, bool extrinsic)
	{
		if(extrinsic)
			return;
		
		switch(address)
		{
		case 0:
		{
			//baseNametable = value & 0x03;
			//scrollX = (scrollX & 0x00FF) | (uint16_t(value & 0x01) << 8);
			//scrollY = (scrollY & 0x00FF) | (uint16_t(value & 0x02) << 7);
			//std::cout << "(" << scanline << ", " << scrollX << ") ";
			tempVRamAccessAddress &= 0xF3FF;
			tempVRamAccessAddress |= uint16_t(value & 0x03) << 10;
			bigVRamIncrementJump = (value & 0x04) != 0;
			useSecondTileTableSprite = (value & 0x08) != 0;
			useSecondTileTableBG = (value & 0x10) != 0;
			tallSprites = (value & 0x20) != 0;

			bool temp = enableNMI;
			enableNMI = (value & 0x80) != 0;
			if(temp != enableNMI && inVBlank)
				requestNMI = true;
			break;
		}

		case 1:
			enableLeftBlankBG = (value & 0x02) != 0;
			enableLeftBlankSprite = (value & 0x04) != 0;
			enableBG = (value & 0x08) != 0;
			enableSprite = (value & 0x10) != 0;
			break;

		case 3:
			OAMAccessAddress = value;
			break;

		case 4:
			switch(OAMAccessAddress & 0x03)
			{
			case 0: OAM[OAMAccessAddress >> 2].y = value; break;
			case 1: OAM[OAMAccessAddress >> 2].tile = value; break;
			case 2:
			{
				int i = OAMAccessAddress >> 2;
				OAM[i].palette = value & 0x03;
				OAM[i].drawBehindBackground = (value & 0x20) != 0;
				OAM[i].flipH = (value & 0x40) != 0;
				OAM[i].flipV = (value & 0x80) != 0;
				break;
			}
			case 3: OAM[OAMAccessAddress >> 2].x = value; break;
			}
			++OAMAccessAddress;
			break;

		case 5:
			if(writeToggle)
				//scrollY = (scrollY & 0xFF00) | value;
				tempVRamAccessAddress = (tempVRamAccessAddress & 0x0C1F) | (uint16_t(value & 0x07) << 12) | (uint16_t(value & 0xF8) << 2);
			else
			{
				//scrollX = (scrollX & 0xFF00) | value;
				tempVRamAccessAddress = (tempVRamAccessAddress & 0xFFE0) | ((value >> 3) & 0x1F);
				tempScrollXFine = value & 0x07;
			}
			writeToggle = !writeToggle;
			break;

		case 6:
			if(writeToggle)
			{
				//VRamAccessAddress = (VRamAccessAddress & 0xFF00) | value;
				tempVRamAccessAddress = (tempVRamAccessAddress & 0xFF00) | value;
				VRamAccessAddress = tempVRamAccessAddress;
			}
			else
				//VRamAccessAddress = (VRamAccessAddress & 0x00FF) | (uint16_t(value & 0x3F) << 8);
				tempVRamAccessAddress = (tempVRamAccessAddress & 0x00FF) | (uint16_t(value & 0x3F) << 8);
			writeToggle = !writeToggle;
			break;

		case 7:
			SetV(VRamAccessAddress & 0x3FFF, value);
			VRamAccessAddress += bigVRamIncrementJump ? 32 : 1;
			break;
		}
	}
	
	void Get(uint16_t address, uint8_t &value, bool extrinsic)
	{
		if(extrinsic)
		{
			value = 0;
			return;
		}

		switch(address)
		{
		case 2:
			value = 0 |
				(uint8_t(hasSpriteOverflow) << 5) |
				(uint8_t(sprite0Hit) << 6) |
				(uint8_t(inVBlank) << 7);

			writeToggle = false;
			inVBlank = false;
			break;

		case 4:
			switch(OAMAccessAddress & 0x03)
			{
			case 0: value = OAM[OAMAccessAddress >> 2].y; break;
			case 1: value = OAM[OAMAccessAddress >> 2].tile; break;
			case 2:
			{
				int i = OAMAccessAddress >> 2;
				value = OAM[i].palette | 
					(uint8_t(OAM[i].drawBehindBackground) << 5) |
					(uint8_t(OAM[i].flipH) << 6) |
					(uint8_t(OAM[i].flipV) << 7);
				break;
			}
			case 3: value = OAM[OAMAccessAddress >> 2].x; break;
			}
			break;
		
		case 7:
			GetV(VRamAccessAddress & 0x3FFF, value);
			VRamAccessAddress += bigVRamIncrementJump ? 32 : 1;
			if(VRamAccessAddress < 0x3F00)
			{
				uint8_t temp = delayVRead;
				delayVRead = value;
				value = temp;
			}
			break;
		}
	}

	void PrepareScanline(int y)
	{
		cacheScanlineSpritesCount = 0;

		for(int i=0 ; i<64 ; ++i)
		{
			Sprite const &s = OAM[i];
			if(y < s.y || s.y + (tallSprites ? 16 : 8) <= y)
				continue;

			ScanlineSprite &ss = cacheScanlineSprites[cacheScanlineSpritesCount];
			++cacheScanlineSpritesCount;

			ss.index = i;
			ss.x = s.x;
			
			if(tallSprites)
			{
				uint16_t tile = (uint16_t(s.tile & 0xFE) << 4) + ((s.tile & 0x01) != 0 ? 0x1000 : 0x0000);
				int yIntersect = y - s.y;
				if(s.flipV) yIntersect = 15 - yIntersect;
				if(yIntersect < 8)
				{
					GetV(tile + uint16_t(yIntersect), ss.plane1);
					GetV(tile + uint16_t(yIntersect) + 8, ss.plane2);
				}
				else
				{
					GetV(tile + uint16_t(yIntersect - 8) + 16, ss.plane1);
					GetV(tile + uint16_t(yIntersect - 8) + 8 + 16, ss.plane2);
				}
			}
			else
			{
				uint16_t tile = (uint16_t(s.tile) << 4) + (useSecondTileTableSprite ? 0x1000 : 0x0000);
				int yIntersect = y - s.y;
				if(s.flipV) yIntersect = 7 - yIntersect;
				GetV(tile + uint16_t(yIntersect), ss.plane1);
				GetV(tile + uint16_t(yIntersect) + 8, ss.plane2);
			}
		}

		if(8 <= cacheScanlineSpritesCount)
			hasSpriteOverflow = true;
	}

	// function can be hevily optimized but remains like this for learning perposes
	// scanline optimization for sprites - done
	// scanline optimization for tiles
	void DoPixel(int x, int y)
	{
		uint8_t pixelColor = palette[0][0][0];
		int tileColor = -1;
		int spriteColor = -1;
		bool isSprite0 = false;
		bool isSpriteBehind = false;

		if(enableBG && !(x < 8 && enableLeftBlankBG))
		{
			uint16_t scrollX = 0 | ((VRamAccessAddress & 0x0400) >> 2) | ((VRamAccessAddress & 0x001F) << 3) | scrollXFine;
			uint16_t scrollY = (0 /*| ((VRamAccessAddress & 0x0800) >> 3)*/ | ((VRamAccessAddress & 0x03E0) >> 2) | ((VRamAccessAddress & 0x7000) >> 12)) + ((VRamAccessAddress & 0x0800) != 0 ? 240 : 0);

			int realX = x + scrollX, realY = y + scrollY;
			//switch(baseNametable)
			//{
			//case 0:  break;
			//case 1: realX += 256; break;
			//case 2: realY += 240; break;
			//case 3: realX += 256; realY += 240; break;
			//}
			realX %= 512;
			realY %= 480;

			uint16_t base;
			if(realY < 240)
			{
				if(realX < 256)
				{
					base = 0x2000;
				}
				else
				{
					base = 0x2400;
					realX -= 256;
				}
			}
			else
			{
				if(realX < 256)
				{
					base = 0x2800;
				}
				else
				{
					base = 0x2C00;
					realX -= 256;
				}
				realY -= 240;
			}

			int tileX = realX / 8, tileY = realY / 8;
			uint8_t tile = 0;
			GetV(base + uint16_t(tileX) + uint16_t(tileY * 32), tile);
			int attrX = realX / 32, attrY = realY / 32;
			uint8_t paletteCluster = 0;
			GetV(base + 0x03C0 + uint16_t(attrX) + uint16_t(attrY * 8), paletteCluster);
			
			uint16_t tileLoc = (useSecondTileTableBG ? 0x1000 : 0x0000) + (uint16_t(tile) << 4);
			int yIntersect = realY - (tileY * 8);
			uint8_t plane1 = 0, plane2 = 0;
			GetV(tileLoc + uint16_t(yIntersect), plane1);
			GetV(tileLoc + uint16_t(yIntersect) + 8, plane2);
			int xIntersect = realX - (tileX * 8);
			int tilePixel = (((plane2 & XIntersectLUT[xIntersect]) != 0) << 1) | ((plane1 & XIntersectLUT[xIntersect]) != 0);
			if(0 < tilePixel)
			{
				int attrXSub = realX / 16, attrYSub = realY / 16;
				int paletteIndex = 0;
				if((attrYSub & 0x01) == 0)
				{
					if((attrXSub & 0x01) == 0)
						paletteIndex = paletteCluster & 0x03;
					else
						paletteIndex = (paletteCluster & 0x0C) >> 2;
				}
				else
				{
					if((attrXSub & 0x01) == 0)
						paletteIndex = (paletteCluster & 0x30) >> 4;
					else
						paletteIndex = (paletteCluster & 0xC0) >> 6;
				}
				tileColor = palette[0][paletteIndex][tilePixel];
			}
		}

		if(enableSprite && !(x < 8 && enableLeftBlankSprite))
		{
			for(int i=0 ; i<cacheScanlineSpritesCount ; ++i)
			{
				ScanlineSprite const &ss = cacheScanlineSprites[i];
				if(x < ss.x || ss.x + 8 <= x)
					continue;
				
				Sprite const &s = OAM[ss.index];
				int xIntersect = x - s.x;
				if(s.flipH) xIntersect = 7 - xIntersect;
				int spritePixel = (((ss.plane2 & XIntersectLUT[xIntersect]) != 0) << 1) | ((ss.plane1 & XIntersectLUT[xIntersect]) != 0);
				if(0 < spritePixel)
				{
					spriteColor = palette[1][s.palette][spritePixel];
					isSprite0 = ss.index == 0;
					isSpriteBehind = s.drawBehindBackground;
					break;
				}
			}
		}

		if(isSprite0 && tileColor != -1 && spriteColor != -1)
			sprite0Hit = true;

		if(spriteColor != -1 && isSpriteBehind)
			pixelColor = uint8_t(spriteColor);
		if(tileColor != -1)
			pixelColor = uint8_t(tileColor);
		if(spriteColor != -1 && !isSpriteBehind)
			pixelColor = uint8_t(spriteColor);

		screen[y][x] = colorLUT[pixelColor];
	}

	void Tick(bool &inFrameLoop, bool &doNMI)
	{
		doNMI |= (requestNMI && enableNMI);
		requestNMI = false;

		if(scanline < 240)
		{
			// visible scanlines
			if(scanlinePosition == 0)
			{
				PrepareScanline(scanline);
			}
			else if(scanlinePosition <= 256)
				DoPixel(scanlinePosition-1, scanline);
			else if(scanlinePosition == 257 && enableBG)
			{
				// if((VRamAccessAddress & 0x7000) != 0x7000)
				// 	VRamAccessAddress += 0x1000;
				// else
				// {
				// 	VRamAccessAddress &= 0x0FFF;
				// 	int y = (VRamAccessAddress & 0x03E0) >> 5;
				// 	if(y == 29)
				// 	{
				// 		y = 0;
				// 		VRamAccessAddress ^= 0x0800;
				// 	}
				// 	else if(y == 31)
				// 		y = 0;
				// 	else
				// 		y += 1;
				// 	VRamAccessAddress = (VRamAccessAddress & 0xFC1F) | (y << 5);
				// }
			}
			else if(scanlinePosition == 258 && enableBG && enableSprite)
			{
				VRamAccessAddress = (VRamAccessAddress & 0xFBE0) | (tempVRamAccessAddress & 0x041F);
				scrollXFine = tempScrollXFine;
			}

			++scanlinePosition;
			if(340 < scanlinePosition)
			{
				++scanline;
				scanlinePosition = 0;
			}
		}
		else if(scanline == 240)
		{
			// Idle scanline
			++scanlinePosition;
			if(340 < scanlinePosition)
			{
				++scanline;
				scanlinePosition = 0;
			}
		}
		else if(scanline <= 260)
		{
			// VBlank
			if(scanline == 241 && scanlinePosition == 1)
			{
				inVBlank = true;
				inFrameLoop = false;
				doNMI = enableNMI;
				//std::cout << "\n";
			}

			++scanlinePosition;
			if(340 < scanlinePosition)
			{
				++scanline;
				scanlinePosition = 0;
			}
		}
		else
		{
			// Dummy scanline
			if(scanlinePosition == 0)
			{
				inVBlank = false;
				hasSpriteOverflow = false;
				sprite0Hit = false;
			}
			else if(scanlinePosition == 257 && enableBG && enableSprite)
				// Copy scrollX related bits
				VRamAccessAddress = (VRamAccessAddress & 0xFBE0) | (tempVRamAccessAddress & 0x041F);
			else if(280 < scanlinePosition && scanlinePosition <= 304 && enableBG && enableSprite)
				// Copy scrollY related bits
				VRamAccessAddress = (VRamAccessAddress & 0x841F) | (tempVRamAccessAddress & 0x7BE0);

			++scanlinePosition;
			if(340 < scanlinePosition - ((!isEvenFrame && enableBG && enableSprite) ? 1 : 0))
			{
				scanline = 0;
				scanlinePosition = 0;
				isEvenFrame = !isEvenFrame;
			}
		}
	}

	void DoOAMCopy(uint8_t *data)
	{
		for(int i=0 ; i<64 ; ++i)
		{
			OAM[i].y = data[0];
			OAM[i].tile = data[1];
			
			OAM[i].palette = data[2] & 0x03;
			OAM[i].drawBehindBackground = (data[2] & 0x20) != 0;
			OAM[i].flipH = (data[2] & 0x40) != 0;
			OAM[i].flipV = (data[2] & 0x80) != 0;
			
			OAM[i].x = data[3];
			data += 4;
		}
	}
};
