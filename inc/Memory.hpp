//
// Created by pp on 10.10.19.
//

#pragma once

#include <vector>
#include <cstdint>
#include <endian.h>
#include <stdexcept>
#include <cmath>
#include <functional>
#include <spdlog/spdlog.h>

namespace esetvm2::core {
  class MemoryError : public std::exception
  {
  public:
    enum class ErrorCode { OffsetOutOfRange, MemoryNotAllocated };

    explicit MemoryError(ErrorCode errorCode)
    : errorCode_(errorCode) {}

    ErrorCode getCode() { return errorCode_; }

  private:
    ErrorCode errorCode_;
  };

  class Memory
  {
  public:
    using ValueType = std::vector<std::byte>;

    void alloc(size_t size)
    {
      data_.resize(size);
    }

    auto getData() { return data_; }

    template <typename T>
    auto read(uint16_t offset) const
    {
      if (data_.empty()) {
        throw MemoryError{MemoryError::ErrorCode::MemoryNotAllocated};
      }

      T value {};

      auto readBegin = data_.begin() + offset;
      auto readEnd = data_.begin() + offset + sizeof(T);

      if (readEnd > data_.end()) {
        throw MemoryError{MemoryError::ErrorCode::OffsetOutOfRange};
      };

      std::copy(readBegin, readEnd, reinterpret_cast<std::byte*>(&value));

      return value;
    }

    auto begin() { return data_.begin(); }
    auto end() { return data_.end(); }

  private:
    ValueType data_;
  };

  class MemBitStream
  {
  public:
    using value_type = std::uint32_t;

    MemBitStream(Memory const * memory, uint16_t offset)
      : memory_(memory)
    {
      auto x = memory_->read<uint32_t>(memOffset_);
      data_ = htobe32(x);
      memOffset_ = sizeof(data_);
      bitsLeft = std::numeric_limits<decltype(data_)>::digits;
    }

    MemBitStream(const MemBitStream& stream) = delete;
    MemBitStream (MemBitStream&& stream) = default;
    MemBitStream& operator= (const MemBitStream& stream) = delete;
    MemBitStream& operator= (MemBitStream&& stream) = default;

    std::function<void()> sync = [](){};

    void loadByte(uint8_t bitsToLoad)
    {
      spdlog::trace("Loading by 8 bits long, bits to load: {0}", bitsToLoad);

      auto x = memory_->read<uint8_t>(memOffset_);
      memOffset_ += sizeof(uint8_t);

      //It is convenient to place the fetched amount of data to the same type
      //as is ValueType, even if the fetched amount of data is only one byte size, although the ValueType is eg. 64bit
      value_type dataToMerge = x;

      //For a while skip bits that are not required.
      //E.g. client request 6 bits, but 2 bits are available.
      //One byte will be fetched, but we need only 4 bits from that byte, so that byte will be shifted to right
      // by 4 bits (to be precise by bitsPerRequestedType - bitsToLoad)

      dataToMerge >>= (std::numeric_limits<uint8_t>::digits - bitsToLoad);
      dataToMerge <<= (std::numeric_limits<value_type>::digits - bitsLeft);
      data_ |= dataToMerge;

      //Sync returns bits that were lost during right shifting operation on dataToMerge
      sync = [this, x, bitsToLoad]() {
        auto d = static_cast<uint8_t>(x << bitsToLoad);
        bitsLeft = static_cast<uint8_t>(std::numeric_limits<uint8_t>::digits - bitsToLoad);
        data_ |= (d << (std::numeric_limits<value_type>::digits - bitsLeft - bitsToLoad));
      };
    };

    void loadWord(uint8_t bitsToLoad)
    {
      spdlog::trace("Loading by 16 bits long, bits to load: {0}", bitsToLoad);

      auto x = htobe16(memory_->read<uint16_t>(memOffset_));
      memOffset_ += sizeof(uint16_t);
      data_ |= (x >> (std::numeric_limits<uint16_t>::digits - bitsToLoad)); // 6 = 8bit - bitsToLoad (2)

      sync = [this, x, bitsToLoad]() {
        auto d = static_cast<uint16_t>(x << bitsToLoad);
        bitsLeft = static_cast<uint8_t>(std::numeric_limits<uint16_t>::digits - bitsToLoad);
        data_ |= (d << (std::numeric_limits<value_type>::digits - bitsLeft - bitsToLoad));
      };
    }


    void loadAtLeastBits(uint8_t bits)
    {

      auto bitsToLoad = static_cast<uint8_t >(std::abs(bitsLeft - bits));

      spdlog::info("Bits left: {0}, bits to load: {1}", bitsLeft, bitsToLoad);

      bitsLeft += bitsToLoad;
      auto mask = 0;

      for (size_t i = 0; i < bitsToLoad; i++) {
        mask = mask << 1;
        mask |= 1;
      }


      //Woks just for 8 bits!!!!!
      auto x = memory_->read<uint8_t>(memOffset_);
      memOffset_ += sizeof(uint8_t);
      data_ |= (x >> 6); // 6 = 8bit - bitsToLoad (2)

      sync = [this, x, bitsToLoad]() {
        auto d = static_cast<uint8_t>(x << bitsToLoad);
        bitsLeft = static_cast<uint8_t>(std::numeric_limits<uint8_t>::digits - bitsToLoad);
        data_ |= (d << (std::numeric_limits<value_type>::digits - bitsLeft - bitsToLoad));
      };
    };

    void loadBits(uint8_t bitsToLoad)
    {
      bitsLeft += bitsToLoad;

      if (bitsToLoad <= 8) {
        loadByte(bitsToLoad);
      }
      else if (bitsToLoad <= 16) {
        loadWord(bitsToLoad);
      }
    }

    template<uint8_t T>
    auto get()
    {
      if ((bitsLeft - T) < 0) {
        auto bitsToLoad = static_cast<uint8_t >(std::abs(bitsLeft - T));
        loadBits(bitsToLoad);

      }

      bitsLeft -= T;

      if constexpr (T <= std::numeric_limits<uint8_t>::digits) {
        auto ret = static_cast<uint8_t>(data_ >> (std::numeric_limits<value_type>::digits - T));
        data_ = data_ << T;

        sync();

        return ret;

      }
      else if constexpr (T <= std::numeric_limits<uint16_t>::digits) {
        auto ret = static_cast<uint16_t>(data_ >> (std::numeric_limits<value_type>::digits - T));
        data_ = data_ << T;

        sync();

        return ret;

      }
      else if constexpr (T <= std::numeric_limits<uint32_t>::digits) {
        auto ret = static_cast<uint32_t>(data_ >> (std::numeric_limits<value_type>::digits - T));
        data_ = data_ << (T - 1);
        data_ = data_ << 1;

        sync();

        return ret;
      }
    }
    uint8_t bitsLeft { 0 };
    value_type data_{ 0 };
    uint16_t memOffset_{ 0 };
    Memory const * memory_; //TODO: Maybe it can be reference
  };
}