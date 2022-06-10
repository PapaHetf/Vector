#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <memory>
#include <algorithm>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory(RawMemory&& other) noexcept {
        buffer_ = std::exchange(other.buffer_(), nullptr);
        capacity_ = std::exchange(other.capacity_, 0u);
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        buffer_ = std::exchange(rhs.buffer_, nullptr);
        capacity_ = std::exchange(rhs.capacity_, 0u);
        return *this;
    }

    T* operator+(size_t offset) noexcept {
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

    explicit Vector(size_t size) : data_(size), size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size_);
    }

    Vector(const Vector& other) : data_(other.size_), size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept {
        data_ = std::move(other.data_);
        size_ = std::exchange(other.size_, 0u);
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            }
            else {
                if (size_ > rhs.Size()) {
                    std::copy_n(rhs.data_.GetAddress(), rhs.Size(), data_.GetAddress());
                    std::destroy_n(data_.GetAddress() + rhs.Size(), size_ - rhs.Size());
                }
                else {
                    std::copy_n(rhs.data_.GetAddress(), size_, data_.GetAddress());
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.Size() - size_, data_.GetAddress() + size_);
                }
            size_ = rhs.Size();
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        data_ = std::move(rhs.data_);
        size_ = std::exchange(rhs.size_, 0u);
        return *this;
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);
        CopyOrMoveData(std::move(new_data));
    }

    void Resize(size_t new_size) {
        if (new_size == size_) {
            return;
        }
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
        }
        else {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
        }
        size_ = new_size;
    }

    template <typename S>
    void PushBack(S&& value) {
        EmplaceBack(std::forward<S>(value));
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
       if (size_ == Capacity()) {
           RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
           new (new_data.GetAddress() + size_) T(std::forward<Args>(args)...);
           CopyOrMoveData(std::move(new_data));
           ++size_;
        }
        else if (size_ < Capacity()) {
            new (data_.GetAddress() + size_) T(std::forward<Args>(args)...);
            ++size_;
        }
       return data_[size_ - 1];
    }

    void PopBack() noexcept {
        assert(size_ > 0);
        std::destroy_n(data_.GetAddress() + (size_ - 1), 1);
        --size_;        
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        assert(pos >= begin() && pos <= end());
        size_t shift_pos = pos - begin();
        if (size_ == Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new (new_data.GetAddress() + shift_pos) T(std::forward<Args>(args)...);
            CopyOrMovePartData(std::move(new_data), const_cast<T*>(pos));
            ++size_;
        }
        else if (size_ < Capacity()) {
            if (pos == end()) {
                new (data_.GetAddress() + size_) T(std::forward<Args>(args)...);
                ++size_;
                return &data_[shift_pos];
            }
            T temp(std::forward<Args>(args)...);
            std::uninitialized_move_n(end() - 1, 1, end());
            std::move_backward(const_cast<T*>(pos), end() - 1, end());
            *const_cast<T*>(pos) = std::move(temp);
            ++size_;
        }
        return &data_[shift_pos];
    }

    template <typename S>
    iterator Insert(const_iterator pos, S&& value) {
        return Emplace(pos, std::forward<S>(value));
    }

    iterator Erase(const_iterator pos) {
        std::move(const_cast<T*>(pos) + 1, end(), const_cast<T*>(pos));
        std::destroy_n(data_.GetAddress() + size_ - 1, 1);
        --size_;
        if (size_ == 0) {
            return end();
        }
        return const_cast<iterator>(pos);
    }

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator begin() const noexcept {
        return  data_.GetAddress();
    }

    const_iterator end() const noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator cend() const noexcept {
        return data_.GetAddress() + size_;
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    void CopyOrMoveData(RawMemory<T>&& new_data) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        data_.Swap(new_data);
        std::destroy_n(new_data.GetAddress(), size_);
    }

    void CopyOrMovePartData(RawMemory<T>&& new_data, iterator pos) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move(begin(), pos, new_data.GetAddress());
            std::uninitialized_move(pos, end(), new_data.GetAddress() + (pos - begin()) + 1);
        }
        else {
            std::uninitialized_copy(begin(), pos, new_data.GetAddress());
            std::uninitialized_copy(pos, end(), new_data.GetAddress() + (pos - begin()) + 1);
        }
        data_.Swap(new_data);
        std::destroy_n(new_data.GetAddress(), size_);
    }
};