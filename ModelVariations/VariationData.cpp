#include "SA.hpp"
#include "VariationData.hpp"

#include <CTheZones.h>

std::array<unsigned short, 65536> originalModels{};


std::unordered_map<unsigned short, std::unordered_map<unsigned short, unsigned short>> variations;
std::unordered_map<unsigned short, std::unordered_map<unsigned short, unsigned short>>::iterator currentZoneVariations = variations.end();

std::unordered_map<uint64_t, std::unordered_map<unsigned short, unsigned short>> interiorVariations;

std::vector<std::vector<unsigned short>> variationSets;


unsigned short variationSetsAdd(const std::vector<unsigned short>& vec)
{
    bool variationSetExists = false;
    size_t setIndex = 0;

    for (size_t j = 0; j < variationSets.size(); j++)
    {
        if (variationSets[j] == vec)
        {
            variationSetExists = true;
            setIndex = j;
            break;
        }
    }

    if (!variationSetExists)
    {
        variationSets.push_back(vec);
        setIndex = variationSets.size() - 1;
    }

    return static_cast<unsigned short>(setIndex);
}

CZone* getZone(std::string_view name)
{
    for (int k = 0; k < CTheZones::TotalNumberOfInfoZones; k++)
    {
        CZone* zone = reinterpret_cast<CZone*>(CTheZones__NavigationZoneArray + k * 0x20);
        if (strncmp(zone->m_szLabel, name.data(), std::min(name.size(), 8U)) == 0)
            return zone;
    }

    return NULL;
}

unsigned short zoneGetIndex(CZone* zone)
{
    return static_cast<unsigned short>((reinterpret_cast<unsigned char*>(zone) - CTheZones__NavigationZoneArray) / 0x20);
}


__declspec(naked) int __stdcall getVariationOriginalModel(int)
{
    __asm
    {
        push    ecx

        mov     eax, [esp + 8]
        mov     ecx, eax
        bswap   ecx
        jcxz    in_range

        pop     ecx
        ret     4

in_range:
        movzx   eax, word ptr[originalModels + eax * 2]
        pop     ecx
        ret     4
    }
}

void resetOriginalModels()
{
    for (size_t i = 0; i < 65536; i++)
        originalModels[i] = static_cast<unsigned short>(i);
}

void setOriginalModel(int model, int originalModel)
{
    if (model > 0 && model < 65536 && originalModel > 0 && originalModel < 65536)
        originalModels[static_cast<unsigned short>(model)] = static_cast<unsigned short>(originalModel);
}
