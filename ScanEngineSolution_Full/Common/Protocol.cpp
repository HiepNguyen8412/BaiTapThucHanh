#include "Protocol.h"

#include <cstring>
#include <limits>

namespace AvProtocol
{
    namespace
    {
        template <typename T>
        void AppendPod(std::vector<std::uint8_t>& output, const T& value)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
            output.insert(output.end(), bytes, bytes + sizeof(T));
        }
    }

    void TlvWriter::AddBytes(FieldType type, const void* data, std::uint32_t length)
    {
        TlvHeader header{};
        header.type = static_cast<std::uint16_t>(type);
        header.length = length;
        AppendPod(data_, header);

        if (length != 0 && data != nullptr)
        {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            data_.insert(data_.end(), bytes, bytes + length);
        }
    }

    void TlvWriter::AddU32(FieldType type, std::uint32_t value)
    {
        AddBytes(type, &value, sizeof(value));
    }

    void TlvWriter::AddU64(FieldType type, std::uint64_t value)
    {
        AddBytes(type, &value, sizeof(value));
    }

    void TlvWriter::AddDouble(FieldType type, double value)
    {
        AddBytes(type, &value, sizeof(value));
    }

    void TlvWriter::AddBool(FieldType type, bool value)
    {
        const std::uint32_t encoded = value ? 1u : 0u;
        AddU32(type, encoded);
    }

    void TlvWriter::AddUtf8(FieldType type, const std::string& value)
    {
        AddBytes(type, value.data(), static_cast<std::uint32_t>(value.size()));
    }

    void TlvWriter::AddWide(FieldType type, const std::wstring& value)
    {
        const auto byteCount = value.size() * sizeof(wchar_t);
        if (byteCount > std::numeric_limits<std::uint32_t>::max())
        {
            return;
        }
        AddBytes(type, value.data(), static_cast<std::uint32_t>(byteCount));
    }

    TlvReader::TlvReader(const std::vector<std::uint8_t>& payload)
    {
        std::size_t offset = 0;
        while (offset + sizeof(TlvHeader) <= payload.size())
        {
            TlvHeader header{};
            std::memcpy(&header, payload.data() + offset, sizeof(header));
            offset += sizeof(header);

            if (header.length > payload.size() - offset)
            {
                fields_.clear();
                return;
            }

            fields_.push_back(FieldView{
                static_cast<FieldType>(header.type),
                payload.data() + offset,
                header.length});
            offset += header.length;
        }

        if (offset != payload.size())
        {
            fields_.clear();
        }
    }

    const TlvReader::FieldView* TlvReader::Find(FieldType type) const
    {
        for (const auto& field : fields_)
        {
            if (field.type == type)
            {
                return &field;
            }
        }
        return nullptr;
    }

    bool TlvReader::GetU32(FieldType type, std::uint32_t& value) const
    {
        const auto* field = Find(type);
        if (field == nullptr || field->length != sizeof(value))
        {
            return false;
        }
        std::memcpy(&value, field->data, sizeof(value));
        return true;
    }

    bool TlvReader::GetU64(FieldType type, std::uint64_t& value) const
    {
        const auto* field = Find(type);
        if (field == nullptr || field->length != sizeof(value))
        {
            return false;
        }
        std::memcpy(&value, field->data, sizeof(value));
        return true;
    }

    bool TlvReader::GetDouble(FieldType type, double& value) const
    {
        const auto* field = Find(type);
        if (field == nullptr || field->length != sizeof(value))
        {
            return false;
        }
        std::memcpy(&value, field->data, sizeof(value));
        return true;
    }

    bool TlvReader::GetBool(FieldType type, bool& value) const
    {
        std::uint32_t encoded = 0;
        if (!GetU32(type, encoded))
        {
            return false;
        }
        value = encoded != 0;
        return true;
    }

    bool TlvReader::GetUtf8(FieldType type, std::string& value) const
    {
        const auto* field = Find(type);
        if (field == nullptr)
        {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(field->data), field->length);
        return true;
    }

    bool TlvReader::GetWide(FieldType type, std::wstring& value) const
    {
        const auto* field = Find(type);
        if (field == nullptr || (field->length % sizeof(wchar_t)) != 0)
        {
            return false;
        }
        value.assign(
            reinterpret_cast<const wchar_t*>(field->data),
            field->length / sizeof(wchar_t));
        return true;
    }

    bool ReadExact(HANDLE handle, void* buffer, DWORD bytes, DWORD& errorCode)
    {
        errorCode = ERROR_SUCCESS;
        auto* output = static_cast<std::uint8_t*>(buffer);
        DWORD total = 0;
        while (total < bytes)
        {
            DWORD read = 0;
            if (!ReadFile(handle, output + total, bytes - total, &read, nullptr))
            {
                errorCode = GetLastError();
                return false;
            }
            if (read == 0)
            {
                errorCode = ERROR_BROKEN_PIPE;
                return false;
            }
            total += read;
        }
        return true;
    }

    bool WriteExact(HANDLE handle, const void* buffer, DWORD bytes, DWORD& errorCode)
    {
        errorCode = ERROR_SUCCESS;
        const auto* input = static_cast<const std::uint8_t*>(buffer);
        DWORD total = 0;
        while (total < bytes)
        {
            DWORD written = 0;
            if (!WriteFile(handle, input + total, bytes - total, &written, nullptr))
            {
                errorCode = GetLastError();
                return false;
            }
            if (written == 0)
            {
                errorCode = ERROR_WRITE_FAULT;
                return false;
            }
            total += written;
        }
        return true;
    }

    bool ReadMessage(HANDLE pipe, Message& message, DWORD& errorCode)
    {
        message = {};
        if (!ReadExact(pipe, &message.header, sizeof(message.header), errorCode))
        {
            return false;
        }
        if (message.header.magic != MAGIC || message.header.version != VERSION)
        {
            errorCode = ERROR_INVALID_DATA;
            return false;
        }
        if (message.header.payloadSize > MAX_PAYLOAD_SIZE)
        {
            errorCode = ERROR_FILE_TOO_LARGE;
            return false;
        }
        message.payload.resize(message.header.payloadSize);
        if (!message.payload.empty() &&
            !ReadExact(pipe, message.payload.data(), message.header.payloadSize, errorCode))
        {
            return false;
        }
        return true;
    }

    bool WriteMessage(
        HANDLE pipe,
        MessageType type,
        std::uint64_t requestId,
        const TlvWriter& payload,
        DWORD& errorCode)
    {
        MessageHeader header{};
        header.magic = MAGIC;
        header.version = VERSION;
        header.type = static_cast<std::uint16_t>(type);
        header.payloadSize = static_cast<std::uint32_t>(payload.Data().size());
        header.requestId = requestId;

        if (!WriteExact(pipe, &header, sizeof(header), errorCode))
        {
            return false;
        }
        if (!payload.Data().empty() &&
            !WriteExact(pipe, payload.Data().data(), header.payloadSize, errorCode))
        {
            return false;
        }
        return true;
    }
}
