#include <gba.h>
#include <gba_video.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_input.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <maxmod.h>

#include "res.h"

#include "soundbank.h"
#include "soundbank_bin.h"

#define BG_MAX_CNT 4

#define SAVE_ADDR ((u8 *)0x0E000000)

#define MAP_WIDTH 32
#define MAP_HEIGHT 32

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160

#define SCREEN_TILE_WIDTH (SCREEN_WIDTH / 8)
#define SCREEN_TILE_HEIGHT (SCREEN_HEIGHT / 8)

#define COIN 1
#define SEEDVILLAGER 2
#define FARMLAND 3
#define FRUITVILLAGER 4
#define HOEVILLAGER 5 // a hoe like in the Minecraft game not like in GTA
#define WATER 6

#define SAVE 99
#define LOAD 98
#define TUTORIAL 97

unsigned short element[100] = {0xF};

uint8_t mapelements[MAP_WIDTH][MAP_HEIGHT] = {0};

uint8_t treeage[MAP_WIDTH][MAP_HEIGHT] = {0};

int16_t xOffset, yOffset;
bool isFacingY = true;
uint8_t taxtime, tax;
uint16_t seeds, fruits, hoes, water, money;

int frames = 0;
bool sec = false;

typedef struct __attribute__((__packed__))
{
  uint32_t magic;
  uint16_t money;
  uint16_t seeds;
  uint16_t fruits;
  uint16_t hoes;
  uint8_t mapelements[MAP_WIDTH][MAP_HEIGHT];
  uint8_t treeage[MAP_WIDTH][MAP_HEIGHT];
  uint8_t taxtime;
  uint8_t tax;
  uint16_t water;
} SavedData;

int rando(int min, int max)
{
  return rand() % (max - min + 1) + min;
}

// We need to make sure to use 8 bits at a time, I kept trying to find out why memcpy won't work fine for hours and only copy the first 8 bits of the whole SavedData struct until I found an obselete site that told me about the problem.

void saveGame(const SavedData *save)
{
  const uint8_t *src = (const uint8_t *)save;
  volatile uint8_t *dst = (volatile uint8_t *)SAVE_ADDR;

  for (int i = 0; i < sizeof(SavedData); i++)
  {
    dst[i] = src[i];
  }
}

void loadGame(SavedData *save)
{
  uint8_t *dst = (uint8_t *)save;
  volatile uint8_t *src = (volatile uint8_t *)SAVE_ADDR;

  for (int i = 0; i < sizeof(SavedData); i++)
  {
    dst[i] = src[i];
  }
}

typedef struct
{
  u32 mapBase;
  u16 *mapBaseAdr;
  u32 tileBase;
  u16 *tileBaseAdr;
} ST_BG;

//---------------------------------------------------------------------------
ST_BG Bg[BG_MAX_CNT];

//---------------------------------------------------------------------------
void WaitForVsync(void)
{
  while (*(vu16 *)0x4000006 >= 160)
  {
  };
  while (*(vu16 *)0x4000006 < 160)
  {
  };
}
//---------------------------------------------------------------------------
void delay(int ms)
{ // The GBA runs at 60Hz, from that we can determine how much frames to skip to
  // achieve the desired delay in milliseconds
  for (int i = 0; i < ms * 60 / 1000; i++)
  {
    WaitForVsync();
    mmFrame(); // Update the music
  }
}
//---------------------------------------------------------------------------
void BgInitMem(void)
{
  const u32 mapBase[] = {8, 9, 13, 14};
  const u32 tileBase[] = {0, 0, 0, 0};
  vs32 i;

  for (i = 0; i < BG_MAX_CNT; i++)
  {
    Bg[i].mapBase = MAP_BASE(mapBase[i]);
    Bg[i].mapBaseAdr = MAP_BASE_ADR(mapBase[i]);
    Bg[i].tileBase = TILE_BASE(tileBase[i]);
    Bg[i].tileBaseAdr = TILE_BASE_ADR(tileBase[i]);
  }

  for (i = 0; i < 32 * 32; i++)
  {
    Bg[0].mapBaseAdr[i] = 0;
    Bg[2].mapBaseAdr[i] = 0;
    Bg[3].mapBaseAdr[i] = 0;
  }

  for (i = 0; i < 64 * 64; i++)
  {
    Bg[1].mapBaseAdr[i] = 0;
  }

  for (i = 0; i < 0x2000; i++)
  {
    Bg[0].tileBaseAdr[i] = 0;
    Bg[1].tileBaseAdr[i] = 0;
    Bg[2].tileBaseAdr[i] = 0;
    Bg[3].tileBaseAdr[i] = 0;
  }
}
//---------------------------------------------------------------------------
void BgInit(void)
{
  irqInit();

  // Maxmod requires the vblank interrupt to reset sound DMA.
  // Link the VBlank interrupt to mmVBlank, and enable it.
  irqSet(IRQ_VBLANK, mmVBlank);
  irqEnable(IRQ_VBLANK);
  BgInitMem();

  REG_DISPCNT = (MODE_0 | BG0_ON | BG1_ON | BG2_ON | BG3_ON);

  REG_BG0CNT = (BG_SIZE_0 | BG_256_COLOR | Bg[0].tileBase | Bg[0].mapBase | 3); // lowest priority (background)
  REG_BG1CNT = (BG_SIZE_3 | BG_256_COLOR | Bg[1].tileBase | Bg[1].mapBase | 2); // higher priority (sprites)
  REG_BG2CNT = (BG_SIZE_0 | BG_256_COLOR | Bg[2].tileBase | Bg[2].mapBase | 1); // higher priority (player and stuff that moves with the player)
  REG_BG3CNT = (BG_SIZE_0 | BG_256_COLOR | Bg[3].tileBase | Bg[3].mapBase | 0); // highest priority (text and UI)
}
//---------------------------------------------------------------------------
void BgSetTile(u16 *pDat, u32 size)
{
  vu32 i;

  for (i = 0; i < size; i++)
  {
    Bg[0].tileBaseAdr[i] = pDat[i];
    Bg[1].tileBaseAdr[i] = pDat[i];
    Bg[2].tileBaseAdr[i] = pDat[i];
    Bg[3].tileBaseAdr[i] = pDat[i];
  }
}
//---------------------------------------------------------------------------
void BgSetPal(u16 *pDat)
{
  vu32 i;

  for (i = 0; i < 256; i++)
  {
    BG_PALETTE[i] = pDat[i];
  }
}
//---------------------------------------------------------------------------
void Bg0SetMap(u16 *pDat, u32 size)
{
  vu32 i;

  for (i = 0; i < size; i++)
  {
    Bg[0].mapBaseAdr[i] = pDat[i];
  }
}
//---------------------------------------------------------------------------
void Bg1SetMap(u16 *pDat, u32 size)
{
  vu32 i;

  for (i = 0; i < size; i++)
  {
    Bg[1].mapBaseAdr[i] = pDat[i];
  }
}
//---------------------------------------------------------------------------
void Bg2SetMap(u16 *pDat, u32 size)
{
  vu32 i;

  for (i = 0; i < size; i++)
  {
    Bg[2].mapBaseAdr[i] = pDat[i];
  }
}
//---------------------------------------------------------------------------
void Bg3SetMap(u16 *pDat, u32 size)
{
  vu32 i;

  for (i = 0; i < size; i++)
  {
    Bg[3].mapBaseAdr[i] = pDat[i];
  }
}
//---------------------------------------------------------------------------

void Bg1SetTile(int x, int y, unsigned short tile)
{
  Bg[1].mapBaseAdr[y * MAP_WIDTH + x] = tile;
}
void Bg2SetTile(int x, int y, unsigned short tile)
{
  Bg[2].mapBaseAdr[y * MAP_WIDTH + x] = tile;
}
void Bg3SetTile(int x, int y, unsigned short tile)
{
  Bg[3].mapBaseAdr[y * MAP_WIDTH + x] = tile;
}

void drawChar(int x, int y, char c)
{
  if (c > 127) // Allow characters below 0 to use special tiles as text
    return;
  if (x < 0 || x >= SCREEN_TILE_WIDTH - 1 || y < 0 || y >= SCREEN_TILE_HEIGHT - 1)
    return;
  if (ResBg3Map[y * MAP_WIDTH + x] - 128 == c)
    return; // If the tile is already set to the character, do not change it
  Bg3SetTile(x, y, 128 + c);
}

void print(int x, int y, const char *text)
{
  while (*text)
  {
    if (*text == '\n') // If we reach a newline character, move to the next line
    {
      x = 0;                           // Reset x to the beginning of the line
      y++;                             // Move to the next line
      if (y >= SCREEN_TILE_HEIGHT - 1) // If we reach the bottom of the screen, stop printing
        return;
      text++;
      continue; // Skip the newline character
    }
    Bg3SetTile(x, y, (char)(128 + *text));
    x++;
    if (x >= SCREEN_TILE_WIDTH - 1)
    {
      x = 0;
      y++;
    }
    text++;
  }
}

// Set a buffer that saves the notice text so we can compare it and not reshow if the text is the same
static char noticeText[90] = {0};

void showNotice(const char *text)
{
  // If the text is the same as the previous one, do not show it again
  if (strcmp(noticeText, text) == 0)
    return;
  strncpy(noticeText, text, sizeof(noticeText) - 1);

  // Show a bar same as the top bar but on the bottom!

  Bg2SetTile(0, SCREEN_TILE_HEIGHT - 4, 12);
  Bg2SetTile(0, SCREEN_TILE_HEIGHT - 3, 20);
  Bg2SetTile(0, SCREEN_TILE_HEIGHT - 2, 20);
  Bg2SetTile(0, SCREEN_TILE_HEIGHT - 1, 28);
  Bg2SetTile(SCREEN_TILE_WIDTH - 1, SCREEN_TILE_HEIGHT - 4, 14);
  Bg2SetTile(SCREEN_TILE_WIDTH - 1, SCREEN_TILE_HEIGHT - 3, 22);
  Bg2SetTile(SCREEN_TILE_WIDTH - 1, SCREEN_TILE_HEIGHT - 2, 22);
  Bg2SetTile(SCREEN_TILE_WIDTH - 1, SCREEN_TILE_HEIGHT - 1, 30);

  for (int x = 1; x < SCREEN_TILE_WIDTH - 1; x++)
  {
    Bg2SetTile(x, SCREEN_TILE_HEIGHT - 4, 13);
    Bg2SetTile(x, SCREEN_TILE_HEIGHT - 3, 21);
    Bg2SetTile(x, SCREEN_TILE_HEIGHT - 2, 21);
    Bg2SetTile(x, SCREEN_TILE_HEIGHT - 1, 29);
  }
  for (int x = 0; x < SCREEN_TILE_WIDTH; x++)
  {
    Bg3SetTile(x, SCREEN_TILE_HEIGHT - 4, 0xF);
    Bg3SetTile(x, SCREEN_TILE_HEIGHT - 3, 0xF);
    Bg3SetTile(x, SCREEN_TILE_HEIGHT - 2, 0xF);
    Bg3SetTile(x, SCREEN_TILE_HEIGHT - 1, 0xF);
  }
  print(0, SCREEN_TILE_HEIGHT - 4, text);
}

void hideNotice()
{
  // If the notice bar is already hidden, do not change it
  if (noticeText[0] == '\0')
    return;
  noticeText[0] = '\0'; // Clear the notice text
  // Hide the notice bar by setting it to empty tiles
  for (int x = 0; x < SCREEN_TILE_WIDTH; x++)
  {
    Bg2SetTile(x, SCREEN_TILE_HEIGHT - 4, 0xF); // Clear the tiles in the notice bar
    Bg2SetTile(x, SCREEN_TILE_HEIGHT - 3, 0xF);
    Bg2SetTile(x, SCREEN_TILE_HEIGHT - 2, 0xF);
    Bg2SetTile(x, SCREEN_TILE_HEIGHT - 1, 0xF);
    Bg3SetTile(x, SCREEN_TILE_HEIGHT - 4, 0xF); // Clear the tiles in the notice bar
    Bg3SetTile(x, SCREEN_TILE_HEIGHT - 3, 0xF);
    Bg3SetTile(x, SCREEN_TILE_HEIGHT - 2, 0xF);
    Bg3SetTile(x, SCREEN_TILE_HEIGHT - 1, 0xF);
  }
}

void changeMoney(uint16_t newMoney)
{
  money = newMoney;
  char buffer[8];
  itoa(money, buffer, 10);
  // Clear 5 tiles for u16
  for (int i = 0; i < 5; i++)
  {
    Bg3SetTile(2 + i, 0, 0xF); // Clear the tiles in the money counter
  }
  print(2, 0, buffer);
}

void changeSeeds(uint16_t newSeeds)
{
  seeds = newSeeds;
  char buffer[8];
  itoa(seeds, buffer, 10);
  // Clear 5 tiles for u16
  for (int i = 0; i < 5; i++)
  {
    Bg3SetTile(10 + i, 0, 0xF); // Clear the tiles in the seeds counter
  }
  print(10, 0, buffer);
}

void changeHoes(uint16_t newHoes)
{
  hoes = newHoes;
  char buffer[8];
  itoa(hoes, buffer, 10);
  // Clear 5 tiles for u16
  for (int i = 0; i < 5; i++)
  {
    Bg3SetTile(18 + i, 0, 0xF); // Clear the tiles in the hoes counter
  }
  print(18, 0, buffer);
}

void changeTax(uint8_t newTax)
{
  tax = newTax;
  char buffer[8];
  itoa(tax, buffer, 10);
  // Clear 3 tiles for u8
  for (int i = 0; i < 3; i++)
  {
    Bg3SetTile(26 + i, 0, 0xF); // Clear the tiles in the tax counter
  }
  print(26, 0, buffer);
}

void changeFruits(uint16_t newFruits)
{
  fruits = newFruits;
  char buffer[8];
  itoa(fruits, buffer, 10);
  // Clear 5 tiles for u16
  for (int i = 0; i < 5; i++)
  {
    Bg3SetTile(2 + i, 1, 0xF); // Clear the tiles in the fruits counter
  }
  print(2, 1, buffer);
}

void changeWater(uint16_t newWater)
{
  water = newWater;
  char buffer[8];
  itoa(water, buffer, 10);
  // Clear 5 tiles for u16
  for (int i = 0; i < 5; i++)
  {
    Bg3SetTile(10 + i, 1, 0xF); // Clear the tiles in the water counter
  }
  print(10, 1, buffer);
}

void changeTaxTime(uint8_t newTaxtime)
{
  taxtime = newTaxtime;
  char buffer[8];
  itoa(taxtime, buffer, 10);
  // Clear 3 tiles for u8
  for (int i = 0; i < 3; i++)
  {
    Bg3SetTile(26 + i, 1, 0xF); // Clear the tiles in the tax time counter
  }
  print(26, 1, buffer);
}

void changeMapElement(int x, int y, uint8_t newElement)
{
  if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT)
    return;
  if (mapelements[x][y] == newElement)
    return; // If the tile is already set to the new element, do not change it
  mapelements[x][y] = newElement;
  Bg1SetTile(x, y, element[newElement]);
}
void changeTreeAge(int x, int y, uint8_t newAge)
{
  if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT)
    return;
  if (mapelements[x][y] != FARMLAND)
    return; // Only change tree age if the tile is farmland
  if (newAge == treeage[x][y])
    return; // If the tree is already at the desired age, do not change it
  treeage[x][y] = newAge;
  if (newAge == 0)
  {
    Bg1SetTile(x, y, element[FARMLAND]); // Base tree
  }
  else if (newAge == 1)
  {
    Bg1SetTile(x, y, 32); // Super Saiyan tree
  }
  else if (newAge == 2)
  {
    Bg1SetTile(x, y, 33); // Super Saiyan 2 tree
  }
  else if (newAge == 3)
  {
    Bg1SetTile(x, y, 34); // Super Saiyan 3 tree
  }
  else if (newAge == 4)
  {
    Bg1SetTile(x, y, 35); // Super Saiyan Blue Super Saiyan tree
  }
  else if (newAge == 5)
  {
    Bg1SetTile(x, y, 36); // Ultra Instinct tree (Mad respect to Copilot for understanding the reference while helping me comment this code)
  }
  else
  {
    Bg1SetTile(x, y, element[FARMLAND]); // Reset to base tree if invalid age
  }
}

void waitForKeyPress()
{
  delay(1000);
  Bg3SetTile(SCREEN_TILE_WIDTH - 3, SCREEN_TILE_HEIGHT - 2, 0xF);
  Bg3SetTile(SCREEN_TILE_WIDTH - 2, SCREEN_TILE_HEIGHT - 2, 39); // Show the A button icon
  while (1)
  {
    scanKeys();
    u16 keys = keysDown();
    if (keys & KEY_A)
    {
      break; // Exit the loop when any key is pressed
    }
    WaitForVsync(); // Wait for the next frame to avoid busy-waiting
    mmFrame();      // Update the music
  }
}

void interact(uint8_t x, uint8_t y)
{
  uint8_t type = mapelements[x][y];
  if (type == SEEDVILLAGER)
  {
    if (money >= 3)
    {
      changeSeeds(seeds + 1);
      changeMoney(money - 3);
    }
  }
  else if (type == FARMLAND)
  {
    if (treeage[x][y] == 0)
    { // Plant a tree if it is empty
      if (seeds > 0)
      {
        changeTreeAge(x, y, 1);
        changeSeeds(seeds - 1);
      }
    }
    else if (treeage[x][y] == 3)
    { // Harvest the tree if it is fully grown
      changeTreeAge(x, y, 0);
      changeFruits(fruits + rando(3, 10));
    }
    else if (treeage[x][y] == 4)
    { // If the tree is in Super Saiyan Blue form, it will give more fruits
      changeTreeAge(x, y, 0);
      changeFruits(fruits + rando(10, 20));
    }
    else if (treeage[x][y] == 5)
    { // If the tree is in Ultra Instinct form, it will give even more fruits
      changeTreeAge(x, y, 0);
      changeFruits(fruits + rando(50, 100));
    }
  }
  else if (type == FRUITVILLAGER)
  {
    if (fruits >= 3)
    {
      changeMoney(money + rando(3, 7));
      changeFruits(fruits - 3);
    }
  }
  else if (type == HOEVILLAGER)
  {
    if (money >= 10)
    {
      changeMoney(money - 10);
      changeHoes(hoes + 1);
    }
  }
  else if (type == WATER)
  {
    if (money)
    {
      changeWater(water + 1000);
      changeMoney(money - 1);
    }
  }
  else if (type == SAVE)
  {
    showNotice("Saving game data...");
    SavedData save;
    save.magic = 0xA212B055;
    save.money = money;
    save.seeds = seeds;
    save.fruits = fruits;
    save.hoes = hoes;
    memcpy(save.mapelements, mapelements, sizeof(mapelements));
    memcpy(save.treeage, treeage, sizeof(treeage));
    save.taxtime = taxtime;
    save.tax = tax;
    save.water = water;
    saveGame(&save);
    showNotice("Save data saved successfully!");
    // Non-blocking wait: show success and wait for a key press
    waitForKeyPress();
    hideNotice();
  }
  else if (type == LOAD)
  {
    showNotice("Loading save data...");
    SavedData save;
    loadGame(&save);
    if (save.magic == 0xA212B055)
    {
      changeMoney(save.money);
      changeSeeds(save.seeds);
      changeFruits(save.fruits);
      changeHoes(save.hoes);
      for (int x = 0; x < MAP_WIDTH; x++)
        for (int y = 0; y < MAP_HEIGHT; y++)
          changeMapElement(x, y, save.mapelements[x][y]);
      memcpy(mapelements, save.mapelements, sizeof(save.mapelements));
      for (int x = 0; x < MAP_WIDTH; x++)
        for (int y = 0; y < MAP_HEIGHT; y++)
          changeTreeAge(x, y, save.treeage[x][y]);
      memcpy(treeage, save.treeage, sizeof(save.treeage));
      changeTaxTime(save.taxtime);
      changeTax(save.tax);
      changeWater(save.water);
      showNotice("Save data loaded successfully!");
      // Non-blocking wait: show success and wait for a key press
      waitForKeyPress();
      hideNotice();
    }
    else
    {
      showNotice("Error loading save data, it either doesn't exist or got corrupted! MAGIC: ");
      char buffer[12];
      itoa(save.magic, buffer, 16);
      print(15, SCREEN_TILE_HEIGHT - 2, buffer);
      // Non-blocking wait: show error and wait for a key press
      waitForKeyPress();
      hideNotice();
    }
  }
  else if (type == TUTORIAL)
  {
    strcpy(noticeText, "\0"); // Clear the notice text so it doesn't show the previous notice

    const char *tutorialText[13] = {// Don't mention villagers since the tiles are just icons of the items and will be confusing, we can just call them shops.
                                    "Welcome to Mapster! A game made by AzizBgBoss, whose mission is to port it to all kind of devices!\n\n\nVersion 1.1",
                                    "In this game, you are a farmer! Your mission is to grow trees, collect fruits, and make money! You can also interact with all kind of shops to help you in your journey!",
                                    "Use your D-Pad to move, and press A to interact with the map elements. Press START to pause the game, and SELECT to toggle music while paused.\n\nTo start, there are different kind of items and shops you need to understand:",
                                    "1. Seeds: On the top bar, you have a seed counter \x83 that represents how many seeds you have. You can buy seeds from the Seed Shop (\x83 icon) for 3 coins \x82 each. You can plant seeds on empty farmland tiles (\x84) to grow trees.",
                                    "2. Hoes: On the top bar, you have a hoe counter \x86 that represents how many hoes you have. You can buy hoes from the Hoe Shop (\x86 icon) for 10 \x82 coins each. You can use hoes to turn normal ground tiles (\x80) to farmland tiles (\x84) to plant trees.",
                                    "3. Fruits: On the top bar, you have a fruit counter \x85 that represents how many fruits you have. You can sell fruits to the Fruit Shop (\x85 icon) for 3 to 7 coins \x82 for each 3 fruits \x85.",
                                    "4. Water: On the top bar, you have a water counter \x87 that represents how much water you have in Liters. You can buy water from the Water Shop (\x87 icon) for 1 \x82 coin each 1000 liters of water \x87.",
                                    "Each tree consumes 1 liter of water \x87 per second, and it can grow to a maximum of 3 ages, each age has a different size and appearance and gives different amount of fruits when harvested.\n\
And that's why you need to keep your water reserves high, other wise the trees will get smaller and die!\n\
Trees grow up in the following order: \n\
        \x84 -> \xA0 -> \xA1 -> \xA2",
                                    "If you're lucky enough, grown up trees \xA2 can turn into Super Strong Blue trees \xA3, which will give you more fruits when harvested!\n\
Same with the Super Strong Blue trees, if you're even more lucky, they can turn into Ultimate Instinct trees \xA4, which will give you even more fruits when harvested!",
                                    "Trees don't consume water \x87 when they grow up \xA2. The special trees \xA3\xA4 will only consume water \x87 when they transform to their state. Then these trees can be harvested for fruits \x85!",
                                    "Finally, you can save your game progress by interacting with the Save \x8A icon and load it later from the Load \x8B icon.",
                                    "I'm sure you will enjoy this game, and I hope you have fun playing it! I'm really fond of programming and it's my favourite hobby. If you have any questions or suggestions, feel free to make an issue or contribute on the GitHub repository:\n\nhttps://github.com/AzizBgBoss/MapsterGBA/",
                                    "Credits:\n\nDeveloper: AzizBgBoss\nSprites: AzizBgBoss\nMusic: DevKitPro's GBA audio example - FlatOutLies by Neil D Voss (1993)\n\nThanks to:\n\n- DevKitPro for the GBA development tools.\n- The gbadev discord server for helping me with the rendering system of the game.\nThanks for playing Mapster!"};

    // Draw borders
    Bg2SetTile(0, 3, 12);                                          // Top left corner
    Bg2SetTile(SCREEN_TILE_WIDTH - 1, 3, 14);                      // Top right corner
    Bg2SetTile(0, SCREEN_TILE_HEIGHT - 1, 28);                     // Bottom left corner
    Bg2SetTile(SCREEN_TILE_WIDTH - 1, SCREEN_TILE_HEIGHT - 1, 30); // Bottom right corner
    // borders
    for (int i = 4; i < SCREEN_TILE_HEIGHT - 2; i++)
    {
      Bg2SetTile(0, i, 20);                     // Left border
      Bg2SetTile(SCREEN_TILE_WIDTH - 1, i, 22); // Right border
    }
    for (int i = 1; i < SCREEN_TILE_WIDTH - 1; i++)
    {
      Bg2SetTile(i, 3, 13);                      // Top border
      Bg2SetTile(i, SCREEN_TILE_HEIGHT - 1, 29); // Bottom
    }

    for (int x = 1; x < SCREEN_TILE_WIDTH - 1; x++)
    {
      for (int y = 4; y < SCREEN_TILE_HEIGHT - 1; y++)
      {
        Bg2SetTile(x, y, 21); // Fill the UI with solid black
      }
    }

    for (int i = 0; i < 13; i++)
    {
      for (int x = 0; x < SCREEN_TILE_WIDTH; x++)
      {
        for (int y = 3; y < SCREEN_TILE_HEIGHT; y++)
        {
          Bg3SetTile(x, y, 0xF); // Clear the text area
        }
      }

      print(0, 3, tutorialText[i]); // Print the tutorial text

      waitForKeyPress();
    }
    // Clear everything after the tutorial
    for (int x = 0; x < SCREEN_TILE_WIDTH; x++)
    {
      for (int y = 3; y < SCREEN_TILE_HEIGHT; y++)
      {
        Bg2SetTile(x, y, 0xF); // Clear the UI
        Bg3SetTile(x, y, 0xF); // Clear the text area
      }
    }
    // Redraw player based on orientation
    if (isFacingY)
    {
      Bg2SetTile((SCREEN_TILE_WIDTH / 2 - 1), (SCREEN_TILE_HEIGHT / 2 - 1), 16);
      Bg2SetTile((SCREEN_TILE_WIDTH / 2 - 1), (SCREEN_TILE_HEIGHT / 2), 24);
      Bg2SetTile((SCREEN_TILE_WIDTH / 2), (SCREEN_TILE_HEIGHT / 2 - 1), 17);
      Bg2SetTile((SCREEN_TILE_WIDTH / 2), (SCREEN_TILE_HEIGHT / 2), 25);
    }
    else
    {
      Bg2SetTile((SCREEN_TILE_WIDTH / 2 - 1), (SCREEN_TILE_HEIGHT / 2 - 1), 18);
      Bg2SetTile((SCREEN_TILE_WIDTH / 2 - 1), (SCREEN_TILE_HEIGHT / 2), 26);
      Bg2SetTile((SCREEN_TILE_WIDTH / 2), (SCREEN_TILE_HEIGHT / 2 - 1), 19);
      Bg2SetTile((SCREEN_TILE_WIDTH / 2), (SCREEN_TILE_HEIGHT / 2), 27);
    }
  }
  else if (type == 0)
  {
    if (hoes)
    {
      changeMapElement(x, y, FARMLAND);
      changeHoes(hoes - 1);
    }
  }
}

void loop()
{
  scanKeys();
  u16 keys = keysHeld();

  bool up = keys & KEY_UP;
  bool down = keys & KEY_DOWN;
  bool left = keys & KEY_LEFT;
  bool right = keys & KEY_RIGHT;

  // Only change facing if moving in one axis
  if ((up || down) && !(left || right))
  {
    if (!isFacingY)
    {
      isFacingY = true;
      Bg2SetTile((SCREEN_TILE_WIDTH / 2 - 1), (SCREEN_TILE_HEIGHT / 2 - 1), 16);
      Bg2SetTile((SCREEN_TILE_WIDTH / 2 - 1), (SCREEN_TILE_HEIGHT / 2), 24);
      Bg2SetTile((SCREEN_TILE_WIDTH / 2), (SCREEN_TILE_HEIGHT / 2 - 1), 17);
      Bg2SetTile((SCREEN_TILE_WIDTH / 2), (SCREEN_TILE_HEIGHT / 2), 25);
    }
  }
  if ((left || right) && !(up || down))
  {
    if (isFacingY)
    {
      isFacingY = false;
      Bg2SetTile((SCREEN_TILE_WIDTH / 2 - 1), (SCREEN_TILE_HEIGHT / 2 - 1), 18);
      Bg2SetTile((SCREEN_TILE_WIDTH / 2 - 1), (SCREEN_TILE_HEIGHT / 2), 26);
      Bg2SetTile((SCREEN_TILE_WIDTH / 2), (SCREEN_TILE_HEIGHT / 2 - 1), 19);
      Bg2SetTile((SCREEN_TILE_WIDTH / 2), (SCREEN_TILE_HEIGHT / 2), 27);
    }
  }

  if (keysDown() & KEY_START)
  {
    showNotice("Paused. Press START to continue.\nPress SELECT to toggle music.");
    while (keysHeld() & KEY_START)
    {
      WaitForVsync(); // Wait for the next frame to avoid busy-waiting
      mmFrame();      // Update the music
      scanKeys();     // Wait until the START key is released
    }

    while (1)
    {
      scanKeys();

      if (keysHeld() & KEY_START)
      {
        while (keysHeld() & KEY_START)
        {
          WaitForVsync(); // Wait for the next frame to avoid busy-waiting
          mmFrame();      // Update the music
          scanKeys();     // Wait until the START key is released
        }
        break;
      }

      if (keysHeld() & KEY_SELECT)
      {
        while (keysHeld() & KEY_SELECT)
        {
          WaitForVsync(); // Wait for the next frame to avoid busy-waiting
          mmFrame();      // Update the music
          scanKeys();     // Wait until the SELECT key is released
        }
        if (mmActive())
          mmPause();
        else
          mmResume();
      }

      WaitForVsync();
      mmFrame();
    }

    hideNotice();
  }

  // Now move
  if (up)
    yOffset++;
  if (down)
    yOffset--;
  if (left)
    xOffset++;
  if (right)
    xOffset--;

  if (xOffset > SCREEN_WIDTH / 2)
    xOffset--;
  else if (xOffset < (SCREEN_WIDTH / 2) - 8 * MAP_WIDTH)
    xOffset++;
  if (yOffset > SCREEN_HEIGHT / 2)
    yOffset--;
  else if (yOffset < (SCREEN_HEIGHT / 2) - 8 * MAP_HEIGHT)
    yOffset++;

  REG_BG0HOFS = -xOffset;
  REG_BG0VOFS = -yOffset;
  REG_BG1HOFS = -xOffset;
  REG_BG1VOFS = -yOffset;

  int waterConsomption = 0;

  if (sec)
  {
    for (uint8_t x = 0; x < MAP_WIDTH; x++)
    {
      for (uint8_t y = 0; y < MAP_HEIGHT; y++)
      {
        switch (mapelements[x][y])
        {
        case FARMLAND:
          if (treeage[x][y] > 0 && treeage[x][y] < 3)
          {
            if (water - waterConsomption > 0)
            {
              waterConsomption++;
              if (rando(0, 100) == 69)
              {
                changeTreeAge(x, y, treeage[x][y] + 1);
              }
            }
            else
            {
              if (rando(0, 50) == 21)
              {
                changeTreeAge(x, y, treeage[x][y] - 1);
              }
            }
          }
          else if (treeage[x][y] == 3)
          {
            if (water - waterConsomption > 10)
            {
              if (rando(0, 1000) == 420)
              {
                waterConsomption += 10;
                changeTreeAge(x, y, treeage[x][y] + 1); // Go to Super Saiyan Blue form. It's legendary, so it never dies if don't water it.
              }
            }
          }
          else if (treeage[x][y] == 4)
          {
            if (water - waterConsomption > 20)
            {
              if (rando(0, 5000) == 341)
              {
                waterConsomption += 20;
                changeTreeAge(x, y, treeage[x][y] + 1); // Go to Ultra Instinct form. It's even more legendary, and it also never dies if don't water it.
              }
            }
          }
          break;
        }
      }
    }

    if (waterConsomption)
      changeWater(water - waterConsomption);
    changeTaxTime(taxtime - 1);

    if (taxtime < 1)
    {
      changeTaxTime(200);
      if (money >= tax)
        changeMoney(money - tax);
      else
        changeMoney(0);
      changeTax(rando(0, 11));
    }
    sec = false;
  }

  for (uint8_t x = 0; x < MAP_WIDTH; x++)
  {
    for (uint8_t y = 0; y < MAP_HEIGHT; y++)
    {
      if (xOffset <= (x * (-8)) + SCREEN_WIDTH / 2 && xOffset > (x * (-8)) + SCREEN_WIDTH / 2 - 7 && yOffset <= (y * (-8)) + SCREEN_HEIGHT / 2 && yOffset > (y * (-8)) + SCREEN_HEIGHT / 2 - 7)
      {
        if (keys & KEY_A)
        {
          interact(x, y);
          while (keys & KEY_A)
          {
            scanKeys();
            keys = keysHeld();
            WaitForVsync();
            mmFrame();
          }
        }
        switch (mapelements[x][y])
        {
          char buffer[96];
        case COIN:
          changeMoney(money + 1);
          changeMapElement(x, y, 0);
          break;
        case SEEDVILLAGER:
          snprintf(buffer, sizeof(buffer), "One seed %c for 3 coins %c", (char)(-128 + 3), (char)(-128 + 2)); // Seed tile and coin tile
          showNotice(buffer);
          break;
        case HOEVILLAGER:
          snprintf(buffer, sizeof(buffer), "One hoe %c for 10 coins %c", (char)(-128 + 6), (char)(-128 + 2)); // Hoe tile and coin tile
          showNotice(buffer);
          break;
        case FRUITVILLAGER:
          snprintf(buffer, sizeof(buffer), "3 to 7 coins %c for 3 fruits %c", (char)(-128 + 2), (char)(-128 + 5)); // Coin tile and fruit tile
          showNotice(buffer);
          break;
        case WATER:
          snprintf(buffer, sizeof(buffer), "1000 liters of water %c for 1 coin %c", (char)(-128 + 7), (char)(-128 + 2)); // Water tile and coin tile
          showNotice(buffer);
          break;
        case SAVE:
          showNotice("Save your game progress.");
          break;
        case LOAD:
          showNotice("Load your saved game progress.");
          break;
        case TUTORIAL:
          showNotice("Press A to open an explanation page that will help you navigate and play the game");
          break;
        case FARMLAND: // Don't say the words! Uncle Sam will be mad!
          if (treeage[x][y] == 4)
          {
            snprintf(buffer, sizeof(buffer), "Super Strong Blue tree %c! It gives about 10 to 20 fruits %c!", (char)(-128 + 35), (char)(-128 + 5)); // Super Saiyan Blue tree tile and fruit tile
            showNotice(buffer);
          }
          else if (treeage[x][y] == 5)
          {
            snprintf(buffer, sizeof(buffer), "Ultimate Instinct tree %c! It gives about 50 to 100 fruits %c!", (char)(-128 + 36), (char)(-128 + 5)); // Ultra Instinct tree tile and fruit tile
            showNotice(buffer);
          }
          else
            hideNotice();
          break;
        default:
          hideNotice(); // Hide the notice if not interacting with any element
          break;
        }
      }
    }
  }
}

int main(void)
{
  element[0] = 0xF;           // Empty tile
  element[COIN] = 2;          // Coin tile
  element[SEEDVILLAGER] = 3;  // Seed villager tile
  element[FARMLAND] = 4;      // Farmland tile
  element[FRUITVILLAGER] = 5; // Fruit villager tile
  element[HOEVILLAGER] = 6;   // Hoe villager tile
  element[WATER] = 7;         // Water tile
  element[SAVE] = 10;         // Save tile
  element[LOAD] = 11;         // Load tile
  element[TUTORIAL] = 47;     // Tutorial tile

  for (int x = 0; x < MAP_WIDTH; x += 2)
  {
    for (int y = 0; y < MAP_HEIGHT; y += 2)
    {
      ResBg0Map[y * MAP_WIDTH + x] = 0;
      ResBg0Map[y * MAP_WIDTH + x + 1] = 1;
      ResBg0Map[(y + 1) * MAP_WIDTH + x] = 8;
      ResBg0Map[(y + 1) * MAP_WIDTH + x + 1] = 9;
    }
  }
  for (int i = 0; i < MAP_WIDTH * MAP_HEIGHT; i++)
  { // Fill with default invisible tile
    ResBg2Map[i] = 0xF;
    ResBg3Map[i] = 0xF;
  }
  for (int i = 0; i < 64 * 64; i++)
  { // Fill with default invisible tile but for bigger buffer
    ResBg1Map[i] = 0xF;
  }

  ResBg2Map[0] = 12;                                     // Top left corner
  ResBg2Map[MAP_WIDTH] = 20;                             // Left border
  ResBg2Map[MAP_WIDTH * 2] = 28;                         // Bottom left corner
  ResBg2Map[SCREEN_TILE_WIDTH - 1] = 14;                 // Top right corner
  ResBg2Map[MAP_WIDTH + SCREEN_TILE_WIDTH - 1] = 22;     // Right border
  ResBg2Map[MAP_WIDTH * 2 + SCREEN_TILE_WIDTH - 1] = 30; // Bottom right corner

  // Fill the top and bottom borders and fill the center with solid black
  for (int x = 1; x < SCREEN_TILE_WIDTH - 1; x++)
  {
    ResBg2Map[x] = 13;
    ResBg2Map[MAP_WIDTH + x] = 21;
    ResBg2Map[MAP_WIDTH * 2 + x] = 29;
  }

  ResBg2Map[(SCREEN_TILE_HEIGHT / 2 - 1) * MAP_WIDTH + (SCREEN_TILE_WIDTH / 2 - 1)] = 16;
  ResBg2Map[(SCREEN_TILE_HEIGHT / 2 - 1) * MAP_WIDTH + (SCREEN_TILE_WIDTH / 2)] = 17;
  ResBg2Map[(SCREEN_TILE_HEIGHT / 2) * MAP_WIDTH + (SCREEN_TILE_WIDTH / 2 - 1)] = 24;
  ResBg2Map[(SCREEN_TILE_HEIGHT / 2) * MAP_WIDTH + (SCREEN_TILE_WIDTH / 2)] = 25;

  BgInit();

  BgSetTile((u16 *)&bg0Tiles, bg0TilesLen);
  BgSetPal((u16 *)&bg0Pal);
  Bg0SetMap((u16 *)&ResBg0Map, BG0_MAP_SIZE / 2);
  Bg1SetMap((u16 *)&ResBg1Map, 64 * 64);
  Bg2SetMap((u16 *)&ResBg2Map, BG0_MAP_SIZE / 2);
  Bg3SetMap((u16 *)&ResBg3Map, BG0_MAP_SIZE / 2);

  REG_BG3HOFS = -4;
  REG_BG3VOFS = -4;

  /*
    //Debug purpose to see the max values of the variables
    money = -1;
    seeds = -1;
    hoes = -1;
    fruits = -1;
    tax = -1;
    water = -1;
    taxtime = -1;
  */

  changeMapElement(0, 0, COIN);
  changeMapElement(5, 7, COIN);
  changeMapElement(28, 31, COIN);
  changeMapElement(16, 8, SEEDVILLAGER);
  changeMapElement(16, 10, FRUITVILLAGER);
  changeMapElement(16, 12, HOEVILLAGER);
  changeMapElement(14, 8, SAVE);
  changeMapElement(14, 10, LOAD);
  changeMapElement(14, 12, WATER);
  changeMapElement(14, 14, TUTORIAL);

  Bg3SetTile(0, 0, 2);   // Money tile
  Bg3SetTile(8, 0, 3);   // Seeds tile
  Bg3SetTile(16, 0, 6);  // Hoes tile
  Bg3SetTile(24, 0, 23); // Tax tile
  Bg3SetTile(0, 1, 5);   // Fruits tile
  Bg3SetTile(8, 1, 7);   // Water tile
  Bg3SetTile(24, 1, 31); // Tax time tile (under tax tile)

  changeMoney(50);
  changeSeeds(0);
  changeHoes(0);
  changeFruits(0);
  changeWater(1000);
  changeTax(0);
  changeTaxTime(200);

  showNotice("Welcome to Mapster v1.1 by AzizbgBoss! Press A to continue...");
  waitForKeyPress();
  showNotice("Move with the D-Pad and press A to interact with the map elements.");
  waitForKeyPress();
  showNotice("Make sure to check the ? icon to read the tutorial and learn how to play the game!");
  waitForKeyPress();
  hideNotice();

  mmInitDefault((mm_addr)soundbank_bin, 8);

  // Start playing module
  mmStart(MOD_FLATOUTLIES, MM_PLAY_LOOP);
  for (;;)
  {
    WaitForVsync();
    mmFrame(); // Update the music

    loop(); // main game logic
    frames++;
    if (frames % 60 == 0)
    { // Every second
      sec = true;
    }
  }
}
