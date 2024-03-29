//
// Created by pp on 02.09.19.
//

#pragma once

#include <array>
#include <exception>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <numeric>
#include <vector>
#include <iterator>
#include <optional>
#include <algorithm>
#include <list>

namespace esetvm2::file_format {

  class EvmFileError : public std::exception
  {
  public:
    enum class ErrorCode { FileNotFound, InvalidSignature, InvalidSize };

    explicit EvmFileError(ErrorCode errorCode)
      : errorCode_(errorCode) {}

  private:
    ErrorCode errorCode_;
  };

  class EvmExecutable {
  public:
    struct Header
    {
      std::array<char, 8> magic;
      uint32_t codeSize;
      uint32_t dataSize;
      uint32_t initialDataSize;

      constexpr size_t size() {
        return sizeof(magic) +
          sizeof(codeSize) +
          sizeof(dataSize) +
          sizeof(initialDataSize);
      }
    };

    class Section
    {
    public:
      enum class Type { Code, Data };

      using iterator = std::vector<std::byte>::iterator;
      using value_type = std::vector<std::byte>::value_type;

      Section(Section::Type type, size_t size)
        : type_(type), size_(size)
        {
          data_.resize(size);
        }

      auto getType() const { return type_; }
      iterator begin() { return data_.begin(); }
      iterator end() { return data_.end(); }
      std::byte* data() { return data_.data(); }

      auto getSize() const { return size_; }

    private:
      Section::Type type_;
      std::vector<std::byte> data_;
      size_t size_;
    };

  public:
    explicit EvmExecutable(const std::string& path)
      : header_{},
        fileSize_{},
        path_(path)
    {
      fillHeader(path);
    }

    const Header& getHeader() const { return header_; }

    Section* const getSection(Section::Type type)
    {
      auto iter = std::find_if(sections_.begin(), sections_.end(),
        [&](auto& x){ return x.getType() == type; });

      if (iter == sections_.end()) {
        return nullptr;
      }

      return &(*iter);
    }

    void loadSections()
    {
      if (!sections_.empty()) {
        return;
      }

      //Skip header
      std::string temp;
      std::ifstream file(path_, std::ios_base::in | std::ios_base::binary);
      file.read(temp.data(), header_.size());

      //Code section
      auto codeSection = Section{Section::Type::Code, header_.codeSize};
      file.read(reinterpret_cast<char*>(codeSection.data()), header_.codeSize);
      sections_.push_back(codeSection);

      //Data section
      if (header_.initialDataSize != 0) {
        auto dataSection = Section(Section::Type::Data, header_.initialDataSize);
        file.read(reinterpret_cast<char*>(dataSection.data()), header_.initialDataSize);

        sections_.push_back(dataSection);
      }
    }

  private:
    void fillHeader(const std::string& path)
    {
      std::ifstream file(path, std::ios_base::in | std::ios_base::binary);

      if (!file) {
        throw EvmFileError(EvmFileError::ErrorCode::FileNotFound);
      }

      file.read(header_.magic.data(), header_.magic.size());

      if (auto magic = std::string(header_.magic.begin(), header_.magic.end()); magic != "ESET-VM2") {
        throw EvmFileError(EvmFileError::ErrorCode::InvalidSignature);
      }

      //Numbers within evm file are stored in little endian.
      //Assuming we are on little endian CPU.
      file.read(reinterpret_cast<char*>(&header_.codeSize), sizeof(header_.codeSize));
      file.read(reinterpret_cast<char*>(&header_.dataSize), sizeof(header_.dataSize));
      file.read(reinterpret_cast<char*>(&header_.initialDataSize), sizeof(header_.initialDataSize));

      fileSize_ = std::filesystem::file_size(path);

      if (!sizeIsValid()) {
        throw EvmFileError(EvmFileError::ErrorCode::InvalidSize);
      }
    }

    bool sizeIsValid()
    {
      return fileSize_ ==
        (header_.codeSize + header_.initialDataSize + header_.size());
    }

  private:
    Header header_{};
    std::vector<Section> sections_;
    uintmax_t fileSize_{};
    std::string path_;
  };
}