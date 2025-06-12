#pragma once

#include <unknwn.h> // For IUnknown

// A simple, self-contained COM smart pointer for RAII (Resource Acquisition Is Initialization).
// This class automatically handles AddRef and Release for COM interface pointers.
template <typename T>
class ComPtr {
public:
    ComPtr() : ptr(nullptr) {}

    ComPtr(T* p) : ptr(p) {
        if (ptr) {
            ptr->AddRef();
        }
    }

    ComPtr(const ComPtr<T>& other) : ptr(other.ptr) {
        if (ptr) {
            ptr->AddRef();
        }
    }

    ~ComPtr() {
        if (ptr) {
            ptr->Release();
        }
    }

    T* operator->() const {
        return ptr;
    }

    T** operator&() {
        // Release the existing pointer before getting the address of a new one.
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
        return &ptr;
    }

    operator T*() const {
        return ptr;
    }

    // Assign a new pointer.
    ComPtr<T>& operator=(T* p) {
        if (ptr != p) {
            if (p) {
                p->AddRef();
            }
            if (ptr) {
                ptr->Release();
            }
            ptr = p;
        }
        return *this;
    }

    // Assign another ComPtr.
    ComPtr<T>& operator=(const ComPtr<T>& other) {
        return *this = other.ptr;
    }

    // Checks if the pointer is valid.
    operator bool() const {
        return ptr != nullptr;
    }

private:
    T* ptr;
};
