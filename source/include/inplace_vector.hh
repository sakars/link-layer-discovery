#ifndef INPLACE_VECTOR_HH
#define INPLACE_VECTOR_HH

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <new>
#include <optional>
#include <utility>

template <class T, int N, size_t Alignment = alignof(T)>
class alignas(Alignment) InplaceVector
{
    static_assert(N > 0);
    union Storage
    {
        uint8_t dummy;
        T value;

        Storage() noexcept : dummy() {}
        Storage(Storage &&) = delete;
        Storage(const Storage &) = delete;
        Storage &operator=(Storage &&) = delete;
        Storage &operator=(const Storage &) = delete;
        ~Storage() {}
    };
    alignas(Alignment) std::array<Storage, N> array_{};
    int size_ = 0;

public:
    InplaceVector() : array_()
    {
    }

    InplaceVector(T data, int amount) : array_(), size_(amount)
    {
        for (int i = 0; i < amount; i++)
        {
            new (&array_[i].value) T(data);
        }
    }

    InplaceVector(const InplaceVector &other)
    {
        for (int i = 0; i < other.size_; i++)
        {
            new (&array_[i].value) T(other.array_[i].value);
        }
        size_ = other.size_;
    }

    InplaceVector(InplaceVector &&other) noexcept
    {
        for (int i = 0; i < other.size_; i++)
        {
            new (&array_[i].value) T(std::move(other.array_[i].value));
        }
        size_ = other.size_;
    }

    InplaceVector &operator=(const InplaceVector &other)
    {
        Clear();
        for (int i = 0; i < other.size_; i++)
        {
            new (&array_[i].value) T(other.array_[i].value);
        }
        size_ = other.size_;
        return *this;
    }

    InplaceVector &operator=(InplaceVector &&other) noexcept
    {
        Clear();
        for (int i = 0; i < other.size_; i++)
        {
            new (&array_[i].value) T(std::move(other.array_[i].value));
        }
        size_ = other.size_;
        return *this;
    }

    ~InplaceVector()
    {
        for (int i = 0; i < size_; i++)
        {
            array_[i].value.~T();
        }
    }

    void Clear()
    {
        for (int i = 0; i < size_; i++)
        {
            array_[i].value.~T();
        }
        size_ = 0;
    }

    std::expected<int, T> TryPushBack(const T &value)
    {
        if (size_ >= N)
        {
            return std::unexpected(T(value));
        }
        new (&array_[size_].value) T(value);
        return size_++;
    }

    std::expected<int, T> TryPushBack(T &&value)
    {
        if (size_ >= N)
        {
            return std::unexpected(std::move(value));
        }
        new (&array_[size_].value) T(std::move(value));
        return size_++;
    }

    void PopBack()
    {
        if (size_ == 0)
        {
            return;
        }
        size_--;
        array_[size_].value.~T();
    }

    void Erase(int idx)
    {
        if (idx < 0 || idx >= size_ || size_ == 0)
        {
            return;
        }
        for (int i = idx; i < size_ - 1; i++)
        {
            array_[i].value = std::move(array_[i + 1].value);
        }
        PopBack();
    }

    int Size() const
    {
        return size_;
    }

    T &operator[](int idx)
    {
        return array_[idx].value;
    }
};

#endif // INPLACE_VECTOR_HH