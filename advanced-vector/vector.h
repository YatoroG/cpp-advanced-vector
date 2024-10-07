#pragma once
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory(RawMemory&& other) noexcept {
        buffer_ = std::exchange(other.buffer_, nullptr);
        capacity_ = std::exchange(other.capacity_, 0);
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        buffer_ = std::exchange(rhs.buffer_, nullptr);
        capacity_ = std::exchange(rhs.capacity_, 0);
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() = default;

    explicit Vector(size_t size)
        : data_(size)
        , size_(size)  //
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_)  //
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept {
        data_ = std::move(other.data_);
        size_ = std::exchange(other.size_, 0);
    }

    ~Vector() {
        Destroy();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_ + size_;
    }

    const_iterator begin() const noexcept {
        return cbegin();
    }

    const_iterator end() const noexcept {
        return cend();
    }

    const_iterator cbegin() const noexcept {
        return const_cast<T*>(data_.GetAddress());
    }

    const_iterator cend() const noexcept {
        return const_cast<T*>(data_ + size_);
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            }
            else {
                std::copy_n(rhs.data_.GetAddress(), std::min(size_, rhs.size_), data_.GetAddress());
                if (rhs.size_ < size_) {
                    std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
                }
                else {
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, data_.GetAddress() + size_);
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            data_ = std::move(rhs.data_);
            size_ = std::exchange(rhs.size_, 0);
        }
        return *this;
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);
        Reallocate(new_data);
        Destroy();
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (size_ != new_size) {
            if (new_size < size_) {
                std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
            }
            else {
                Reserve(new_size);
                std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
            }
            size_ = new_size;
        }
    }

    void PushBack(const T& value) {
        if (data_.Capacity() == size_) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            std::uninitialized_copy_n(&value, 1, new_data.GetAddress() + size_);
            Reallocate(new_data);
            Destroy();
            data_.Swap(new_data);
        }
        else {
            new (data_ + size_) T(value);
        }
        size_++;
    }

    void PushBack(T&& value) {
        if (data_.Capacity() == size_) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            std::uninitialized_move_n(&value, 1, new_data.GetAddress() + size_);
            Reallocate(new_data);
            Destroy();
            data_.Swap(new_data);
        }
        else {
            new (data_ + size_) T(std::move(value));
        }
        size_++;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (data_.Capacity() == size_) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new (new_data + size_) T(std::forward<Args>(args)...);
            Reallocate(new_data);
            Destroy();
            data_.Swap(new_data);
        }
        else {
            new (data_ + size_) T(std::forward<Args>(args)...);
        }
        size_++;
        return *std::prev(end());
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        int pos_num = pos - begin();
        if (data_.Capacity() == size_) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new (new_data + pos_num) T(std::forward<Args>(args)...);
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(data_.GetAddress(), pos_num, new_data.GetAddress());
                std::uninitialized_move_n(data_.GetAddress() + pos_num, size_ - pos_num, new_data.GetAddress() + pos_num + 1);
            }
            else {
                std::uninitialized_copy_n(data_.GetAddress(), pos_num, new_data.GetAddress());
                std::uninitialized_copy_n(data_.GetAddress() + pos_num, size_ - pos_num, new_data.GetAddress() + pos_num + 1);
            }
            Destroy();
            data_.Swap(new_data);
        }
        else {
            T new_elem(std::forward<Args>(args)...);
            new (end()) T(std::forward<T>(data_[size_ - 1]));
            std::move_backward(begin() + pos_num, end() - 1, end());
            *(begin() + pos_num) = std::forward<T>(new_elem);
        }
        size_++;
        return begin() + pos_num;
    }

    iterator Erase(const_iterator pos) /*noexcept(std::is_nothrow_move_assignable_v<T>)*/ {
        int pos_num = pos - begin();
        std::move(begin() + pos_num + 1, end(), begin() + pos_num);
        std::destroy_at(end() - 1);
        size_--;
        return begin() + pos_num;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    void PopBack() /* noexcept */ {
        if (size_ != 0) {
            std::destroy_n(data_.GetAddress() + size_, 1);
            size_--;
        }
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    void Destroy() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    void Reallocate(RawMemory<T>& new_data) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
    }
};