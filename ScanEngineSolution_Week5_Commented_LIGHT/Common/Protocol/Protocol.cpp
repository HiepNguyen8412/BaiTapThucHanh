// ============================================================================
// MODULE : Common / Protocol
// ROLE   : Serialize/deserialize framed messages over byte-mode Named Pipe.
// WEEK 5 : ReadExact handles partial reads; payloadSize separates sticky packets;
//          CRC32 detects damaged frames.
// ============================================================================

#include "Protocol/Protocol.h"
#include "Protocol/Crc32.h"

#include <cstring>
#include <limits>
#include <algorithm>

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

    // Dong goi mot field theo TLV: Type + Length + Value vao payload.
    // Moi field tu mo ta kich thuoc nen ben nhan co the parse noi tiep nhieu field.
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

    void TlvWriter::AddU32(FieldType type, std::uint32_t value) { AddBytes(type, &value, sizeof(value)); }
    void TlvWriter::AddU64(FieldType type, std::uint64_t value) { AddBytes(type, &value, sizeof(value)); }
    void TlvWriter::AddDouble(FieldType type, double value) { AddBytes(type, &value, sizeof(value)); }
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
        if (byteCount <= std::numeric_limits<std::uint32_t>::max())
        {
            AddBytes(type, value.data(), static_cast<std::uint32_t>(byteCount));
        }
    }

    // Parse toan bo payload TLV mot lan, luu cac FieldView de cac ham Get... truy cap sau do.
    // Neu length vuot qua payload thi danh dau reader invalid de chan du lieu bi cat/hong.
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
                valid_ = false;
                return;
            }
            fields_.push_back(FieldView{
                static_cast<FieldType>(header.type), payload.data() + offset, header.length });
            offset += header.length;
        }
        if (offset != payload.size())
        {
            fields_.clear();
            valid_ = false;
        }
    }

    // Tim field dau tien co dung FieldType; cac GetU32/GetU64/... deu dung ham nay.
    const TlvReader::FieldView* TlvReader::Find(FieldType type) const
    {
        if (!valid_) return nullptr;
        for (const auto& field : fields_)
        {
            if (field.type == type) return &field;
        }
        return nullptr;
    }

    bool TlvReader::GetU32(FieldType type, std::uint32_t& value) const
    {
        const auto* field = Find(type);
        if (field == nullptr || field->length != sizeof(value)) return false;
        std::memcpy(&value, field->data, sizeof(value));
        return true;
    }
    bool TlvReader::GetU64(FieldType type, std::uint64_t& value) const
    {
        const auto* field = Find(type);
        if (field == nullptr || field->length != sizeof(value)) return false;
        std::memcpy(&value, field->data, sizeof(value));
        return true;
    }
    bool TlvReader::GetDouble(FieldType type, double& value) const
    {
        const auto* field = Find(type);
        if (field == nullptr || field->length != sizeof(value)) return false;
        std::memcpy(&value, field->data, sizeof(value));
        return true;
    }
    bool TlvReader::GetBool(FieldType type, bool& value) const
    {
        std::uint32_t encoded = 0;
        if (!GetU32(type, encoded)) return false;
        value = encoded != 0;
        return true;
    }
    bool TlvReader::GetUtf8(FieldType type, std::string& value) const
    {
        const auto* field = Find(type);
        if (field == nullptr) return false;
        value.assign(reinterpret_cast<const char*>(field->data), field->length);
        return true;
    }
    bool TlvReader::GetWide(FieldType type, std::wstring& value) const
    {
        const auto* field = Find(type);
        if (field == nullptr || (field->length % sizeof(wchar_t)) != 0) return false;
        value.assign(reinterpret_cast<const wchar_t*>(field->data), field->length / sizeof(wchar_t));
        return true;
    }

    //Partial Read
    // QUAN TRONG: ReadFile co the chi tra ve MOT PHAN du lieu yeu cau.
    // Vong lap tiep tuc doc den khi du bytes -> xu ly duoc partial read cua Named Pipe.
    bool ReadExact(HANDLE handle, void* buffer, DWORD bytes, DWORD& errorCode)
    {
        errorCode = ERROR_SUCCESS;
        auto* output = static_cast<std::uint8_t*>(buffer);
        DWORD total = 0;

		constexpr DWORD READ_TIMEOUT_MS = 5000; // 5 giay;

		ULONGLONG lastDataTime = GetTickCount64();

        while (total < bytes)
        {
            // Kiểm tra xem Pipe hiện có bao nhiêu byte đang chờ đọc.
            DWORD available = 0;

            if (!PeekNamedPipe(
                handle,
                nullptr,
                0,
                nullptr,
                &available,
                nullptr))
            {
                errorCode = GetLastError();
                return false;
            }
                // Chưa có dữ liệu mới
            if (available == 0)
            {
                //Nếu đã qua 5 giây vẫn không có thêm dữ liệu => Dừng chờ. 
                if (GetTickCount64() - lastDataTime > READ_TIMEOUT_MS)
                {
                    errorCode = ERROR_TIMEOUT;
                    return false;
                }
                Sleep(10);
                continue;
            }

            // Số byte còn thiếu 
            const DWORD remaining = bytes - total;

			// Chỉ đọc tối đa phần còn thiếu, không đọc lấn sang frame tiếp theo.
            const DWORD toRead = 
				(std::min)(remaining, available);

            DWORD read = 0;
            
            if (!ReadFile(
               handle,
               output + total,
               toRead,
               &read,
               nullptr))
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

            // Đã nhận thêm dữ liệu => reset mốc Timeout.
			lastDataTime = GetTickCount64();
        }

            return true;
    }

        // Tuong tu ReadExact: lap WriteFile cho den khi gui het buffer.
        // Nho vay mot frame lon khong bi coi la da gui xong khi Windows moi ghi mot phan.
        bool WriteExact(HANDLE handle, const void* buffer, DWORD bytes, DWORD & errorCode)
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

        //Sticky Packet
        // Doc 1 frame day du theo thu tu: HEADER -> validate magic/version/size -> PAYLOAD -> CRC.
        // Length-prefix trong header giup tach duoc cac goi bi dinh nhau (sticky packets).
        bool ReadMessage(HANDLE pipe, Message & message, DWORD & errorCode)
        {
            message = {};
            if (!ReadExact(pipe, &message.header, sizeof(message.header), errorCode)) return false;
            if (message.header.magic != MAGIC)
            {
                errorCode = ERROR_INVALID_DATA;
                return false;
            }
            if (message.header.version != VERSION)
            {
                errorCode = ERROR_REVISION_MISMATCH;
                return false;
            }
            if (message.header.payloadSize > MAX_PAYLOAD_SIZE)
            {
                errorCode = ERROR_BUFFER_OVERFLOW;
                return false;
            }

            message.payload.resize(message.header.payloadSize);
            if (!message.payload.empty() &&
                !ReadExact(pipe,
                    message.payload.data(),
                    message.header.payloadSize,
                    errorCode))

            {
                return false;
            }

            const std::uint32_t actualChecksum = message.payload.empty()
                ? 0u
                : CalculateCrc32(message.payload.data(), message.payload.size());
            if (actualChecksum != message.header.checksum)
            {
                errorCode = ERROR_CRC;
                return false;
            }
            errorCode = ERROR_SUCCESS;
            return true;
        }

        // Tao MessageHeader, gan type/requestId/payloadSize/checksum roi gui header + payload.
        // Ben client/service deu dung cung ham nay nen framing cua hai phia luon dong nhat.
        bool WriteMessage(
            HANDLE pipe,
            MessageType type,
            std::uint64_t requestId,
            const TlvWriter & payload,
            DWORD & errorCode)
        {
            if (payload.Data().size() > MAX_PAYLOAD_SIZE)
            {
                errorCode = ERROR_BUFFER_OVERFLOW;
                return false;
            }

            MessageHeader header{};
            header.magic = MAGIC;
            header.version = VERSION;
            header.type = static_cast<std::uint16_t>(type);
            header.payloadSize = static_cast<std::uint32_t>(payload.Data().size());
            header.requestId = requestId;
            header.checksum = payload.Data().empty()
                ? 0u
                : CalculateCrc32(payload.Data().data(), payload.Data().size());

            if (!WriteExact(pipe, &header, sizeof(header), errorCode)) return false;
            if (!payload.Data().empty() &&
                !WriteExact(pipe, payload.Data().data(), header.payloadSize, errorCode)) return false;
            return true;
        }
 }