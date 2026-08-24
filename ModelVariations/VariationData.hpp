#pragma once

#include <CZone.h>

#include <array>
#include <unordered_map>
#include <string_view>
#include <vector>


extern std::unordered_map<unsigned short, std::unordered_map<unsigned short, unsigned short>> variations;
extern std::unordered_map<unsigned short, std::unordered_map<unsigned short, unsigned short>>::iterator currentZoneVariations;

extern std::unordered_map<uint64_t, std::unordered_map<unsigned short, unsigned short>> interiorVariations;

extern std::vector<std::vector<unsigned short>> variationSets;

unsigned short variationSetsAdd(const std::vector<unsigned short>& vec);
CZone* getZone(std::string_view name);
unsigned short zoneGetIndex(CZone* zone);

int __stdcall getVariationOriginalModel(int);
void resetOriginalModels();
void setOriginalModel(int model, int originalModel);
