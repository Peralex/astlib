///
/// \package astlib
/// \file BinaryDataDekoder.cpp
///
/// \author Marian Krivos <nezmar@tutok.sk>
/// \date 6Feb.,2017 
///
/// (C) Copyright 2017 R-SYS s.r.o
/// All rights reserved.
///

#include "BinaryAsterixDecoder.h"

#include "model/FixedItemDescription.h"
#include "model/VariableItemDescription.h"
#include "model/RepetitiveItemDescription.h"
#include "model/ExplicitItemDescription.h"
#include "model/CompoundItemDescription.h"
#include "Exception.h"

#include <Poco/ByteOrder.h>
#include <iostream>

namespace astlib
{

BinaryAsterixDecoder::BinaryAsterixDecoder(CodecPolicy policy) :
    _policy(policy)
{
}

BinaryAsterixDecoder::~BinaryAsterixDecoder()
{
}

void BinaryAsterixDecoder::decode(const CodecDescription& codec, ValueDecoder& valueDecoder, const Byte buf[], size_t bytes)
{
    if (bytes < 6)
    {
        throw Exception("Too short message in BinaryDataDekoder::decode()");
    }

    const unsigned short *sizePtr;
    const Byte *fspecPtr;
    unsigned index = 1;

    sizePtr = (unsigned short *)(buf+index);
    // TODO: brat endian z codec
    int size = Poco::ByteOrder::fromNetwork(*sizePtr);
    index += 2;

    size -= index;

    if (size < 2 || size > MAX_PACKET_SIZE)
    {
        throw Exception("Bad size of subpacket in BinaryDataDekoder::decode()");
    }

    while(1)
    {
        fspecPtr = (Byte *)buf+index;

        if (size == 0)
            break;

        int len = decodeRecord(codec, valueDecoder, fspecPtr);

        // Chyba, treba vyskocit inak bude nekonecna slucka
        if (len <= 0)
            throw Exception("BinaryDataDekoder::decode(): buffer underflow");

        index += len;
        size -= len;

        if (index > bytes)
            throw Exception("BinaryDataDekoder::decode(): buffer overflow - codec eats " + std::to_string(index-bytes) + " more bytes");

        //if (index < bytes) throw Exception("BinaryDataDekoder::decode(): buffer underflow - codec eats " + std::to_string(bytes-index) + " less bytes");

        if (index == bytes)
            break;
    }
}

int BinaryAsterixDecoder::decodeRecord(const CodecDescription& codec, ValueDecoder& valueDecoder, const Byte fspecPtr[])
{
    const Byte* startPtr = fspecPtr;
    size_t fspecLen = ByteUtils::calculateFspec(fspecPtr);

    if (fspecPtr[0] == 0)
        throw Exception("Bad FSPEC[0] value for decoded message in AsterixCategory::decodeMessageERA()");

    const Byte *localPtr = fspecPtr + fspecLen;
    int fspecMask = 0x80;
    int currentFspecBit = 0;

    // Loop for all fspec bits
    const CodecDescription::UapItems& uapItems = codec.enumerateUapItems();

    valueDecoder.begin(codec.getCategoryDescription().getCategory());

    for (size_t i = 0; i < fspecLen; i++)
    {
        for(int j = 0; j < 8; j++)
        {
            bool bitPresent = (fspecMask & *fspecPtr);  // priznak pritomnosti aktualne testovaneho FSPEC bitu
            int decodedByteCount = 0;

            if (fspecMask & FX_BIT)
            {
                fspecMask = 0x80;
                // Sme v prechode na dalsi FSPEC bajt
                if (bitPresent == false)
                {
                    // Definitivne koncime
                    break;
                }

                fspecPtr++;
                currentFspecBit++;
                continue;
            }

            auto iterator = uapItems.find(currentFspecBit);
            if (iterator == uapItems.end() || (!iterator->second.item && bitPresent))
                throw Exception("Undefined Data Item for bit " + std::to_string(currentFspecBit));

            const ItemDescription& uapItem = *iterator->second.item;
            bool mandatory = iterator->second.mandatory;

            if (bitPresent)
            {
                valueDecoder.beginItem(uapItem);

                if (_policy.verbose)
                    std::cout << "Decode " << (mandatory?"mandatory ":"optional ") << uapItem.getType().toString() << " " << codec.getCategoryDescription().getCategory() << "/" << uapItem.getId() << ": " << uapItem.getDescription() << std::endl;

                _depth++;

                switch(uapItem.getType().toValue())
                {
                    case ItemFormat::Fixed:
                        decodedByteCount += decodeFixed(uapItem, valueDecoder, localPtr);
                        break;

                    case ItemFormat::Variable:
                        decodedByteCount += decodeVariable(uapItem, valueDecoder, localPtr);
                        break;

                    case ItemFormat::Repetitive:
                        decodedByteCount += decodeRepetitive(uapItem, valueDecoder, localPtr);
                        break;

                    case ItemFormat::Compound:
                        decodedByteCount += decodeCompound(uapItem, valueDecoder, localPtr);
                        break;

                    case ItemFormat::Explicit:
                        decodedByteCount += decodeExplicit(uapItem, valueDecoder, localPtr);
                        break;

                }

                --_depth;
            }
            else if (mandatory)
            {
                // TODO: nepritomna ale povinna polozka ...
            }

            if (_policy.verbose && decodedByteCount>0)
            {
                for(int i = 0; i < decodedByteCount; i++)
                {
                    std::cout << " " << Poco::NumberFormatter::formatHex(localPtr[i], 2, false);
                }
                std::cout << std::endl;
                std::cout << "  " << " Stream advance " << decodedByteCount << " bytes" << std::endl;
            }

            localPtr += decodedByteCount;
            currentFspecBit++;
            fspecMask >>= 1;
        }
    }

    valueDecoder.end();

    return int(localPtr-startPtr);
}

int BinaryAsterixDecoder::decodeFixed(const ItemDescription& uapItem, ValueDecoder& valueDecoder, const Byte data[])
{
    const FixedItemDescription& fixedItem = static_cast<const FixedItemDescription&>(uapItem);
    const Fixed& fixed = fixedItem.getFixed();
    decodeBitset(uapItem, fixed, data, valueDecoder, -1, 0);
    return fixed.length;
}

int BinaryAsterixDecoder::decodeVariable(const ItemDescription& uapItem, ValueDecoder& valueDecoder, const Byte data[])
{
    const VariableItemDescription& varItem = static_cast<const VariableItemDescription&>(uapItem);
    const FixedVector& fixedVector = varItem.getFixedVector();
    auto ptr = data;
    int decodedByteCount = 0;

    for(;;)
    {
        Byte fspecBit;
        for(const Fixed& fixed: fixedVector)
        {
            auto len = fixed.length;
            fspecBit = (ptr[len-1] & FX_BIT);

            decodeBitset(uapItem, fixed, ptr, valueDecoder, -1, 0);
            decodedByteCount += len;
            ptr += len;
            if (fspecBit == 0)
                break;
        }
        if (fspecBit == 0)
            break;
    }

    return decodedByteCount;
}

int BinaryAsterixDecoder::decodeRepetitive(const ItemDescription& uapItem, ValueDecoder& valueDecoder, const Byte data[])
{
    const RepetitiveItemDescription& varItem = static_cast<const RepetitiveItemDescription&>(uapItem);
    const FixedVector& fixedVector = varItem.getFixedVector();
    int decodedByteCount = 1;
    int counter = *data;
    auto ptr = data+1;

    // TODO: zrusit?
    valueDecoder.beginRepetitive(counter);

    for(int j = 0; j < counter; j++)
    {
        // TODO: zrusit?
        valueDecoder.repetitiveItem(j);
        for(const Fixed& fixed: fixedVector)
        {
            decodeBitset(uapItem, fixed, ptr, valueDecoder, j, counter);
            decodedByteCount += fixed.length;
            ptr += fixed.length;
        }
    }

    // TODO: zrusit?
    valueDecoder.endRepetitive();

    return decodedByteCount;
}

int BinaryAsterixDecoder::decodeExplicit(const ItemDescription& uapItem, ValueDecoder& valueDecoder, const Byte data[])
{
    const ExplicitItemDescription& varItem = static_cast<const ExplicitItemDescription&>(uapItem);
    const FixedVector& fixedVector = varItem.getFixedVector();
    int decodedByteCount = 1;
    int counter = *data -1;
    auto ptr = data+1;

    // TODO: zrusit?
    valueDecoder.beginRepetitive(counter);

    for(int j = 0; j < counter; j++)
    {
        // TODO: zrusit?
        valueDecoder.repetitiveItem(j);
        for(const Fixed& fixed: fixedVector)
        {
            decodeBitset(uapItem, fixed, ptr, valueDecoder, j, counter);
            decodedByteCount += fixed.length;
            ptr += fixed.length;
        }
    }

    // TODO: zrusit?
    valueDecoder.endRepetitive();

    return decodedByteCount;
}

int BinaryAsterixDecoder::decodeCompound(const ItemDescription& uapItem, ValueDecoder& valueDecoder, const Byte data[])
{
    const CompoundItemDescription& compoundItem = static_cast<const CompoundItemDescription&>(uapItem);
    const ItemDescriptionVector& items = compoundItem.getItemsVector();
    int allByteCount = 0;
    auto itemCount = items.size();
    ItemDescriptionVector usedItems;
    size_t itemIndex = 1; // zero index is for Variable item itself

    poco_assert(itemCount);
    poco_assert(items[0]->getType() == ItemFormat::Variable);

    for(;;)
    {
        Byte fspec = data[0];
        int mask = 0x80;
        for(int j = 0; j < 7; j++)
        {
            if (fspec & mask)
            {
                poco_assert(itemIndex < items.size());
                poco_assert(items[itemIndex]);
                usedItems.push_back(items[itemIndex]);
            }
            mask >>= 1;
            itemIndex++;
        }

        data++;
        allByteCount++;

        if ((fspec & FX_BIT) == 0)
            break;
    }

    auto usedItemsCount = usedItems.size();
    for(size_t i = 0; i < usedItemsCount; i++)
    {
        const ItemDescription& uapItem = *usedItems[i];
        int decodedByteCount = 0;
        //valueDecoder.item(uapItem);

        switch(uapItem.getType().toValue())
        {
            case ItemFormat::Fixed:
                decodedByteCount = decodeFixed(uapItem, valueDecoder, data);
                break;

            case ItemFormat::Variable:
                decodedByteCount = decodeVariable(uapItem, valueDecoder, data);
                break;

            case ItemFormat::Repetitive:
                decodedByteCount = decodeRepetitive(uapItem, valueDecoder, data);
                break;

            default:
                throw Exception("Unhandled SubItem type: " + uapItem.getType().toString());
        }
        data += decodedByteCount;
        allByteCount += decodedByteCount;
    }

    return allByteCount;
}

void BinaryAsterixDecoder::decodeBitset(const ItemDescription& uapItem, const Fixed& fixed, const Byte* localPtr, ValueDecoder& valueDecoder, int index, int arraySize)
{
    const BitsDescriptionArray& bitsDescriptions = fixed.bitsDescriptions;
    Poco::UInt64 data = 0;
    Poco::UInt64 data2 = 0;
    Poco::UInt64 data3 = 0;
    int length = fixed.length;
    Poco::UInt64 value;

    for (int i = 0; i < length; i++)
    {
        data3 <<= 8;
        data3 |= ((data2 >> 56) & 0xFF);
    	data2 <<= 8;
    	data2 |= ((data >> 56) & 0xFF);
        data <<= 8;
        data |= localPtr[i];
    }

    for (const BitsDescription& bits : bitsDescriptions)
    {
        CodecContext context(uapItem, _policy, bits, _depth);
        bool over64bits = (bits.to + context.width) > 64;
        bool over128bits = (bits.to + context.width) > 128;

        if (_policy.verbose)
        {
            std::cout << "  decode " << bits.toString() << std::endl;
        }

        if (context.width == 1)
        {
            int lowBit = bits.bit - 1;
            // Send non FX bits only
            if (!bits.fx)
            {
                if (!over64bits && !over128bits)
                	value = ((data >> lowBit) & 1);
                else if (over64bits && !over128bits)
                	value = ((data2 >> (lowBit-64)) & 1);
                else
                    value = ((data3 >> (lowBit - 128)) & 1);

                if (index == 0)
                {
                    // Preinitialize array
                    valueDecoder.beginArray(bits.code, arraySize);
                }
                valueDecoder.decode(context, value, index);
            }
        }
        else
        {
            Poco::UInt64 mask = bits.bitMask();
            int lowBit = bits.to - 1;

            if (!over64bits && !over128bits)
            {
            	value = ((data >> lowBit) & mask);
            }
            else if (over64bits && !over128bits)
            {
            	if (lowBit >= 64)
            	{
            		value = ((data2 >> (lowBit-64)) & mask);
            	}
            	else
            	{
            		// partially data and data2
            		int shift1 = lowBit;
            		int shift2 = 64-lowBit;
            		Poco::UInt64 aux1 = ((data & (mask<<lowBit)) >> shift1);
            		Poco::UInt64 aux2 = (data2 << shift2) & mask;
            		value = (aux1 | aux2) & mask;
            	}
            }
            else {
                if (lowBit >= 128)
                {
                    value = ((data3 >> (lowBit - 128)) & mask);
                }
                else
                {
                    // partially data2 and data3
                    int shift1 = lowBit;
                    int shift2 = 128 - lowBit;
                    Poco::UInt64 aux1 = ((data2 & (mask << lowBit)) >> shift1);
                    Poco::UInt64 aux2 = (data3 << shift2) & mask;
                    value = (aux1 | aux2) & mask;
                }
            }

            if (index == 0)
            {
                // Preinitialize array
                valueDecoder.beginArray(bits.code, arraySize);
            }
            valueDecoder.decode(context, value, index);
        }
    }
}

} /* namespace astlib */
