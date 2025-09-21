#pragma once
#include <unknwn.h>
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
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
        return &ptr;
    }
    operator T* () const {
        return ptr;
    }
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
    ComPtr<T>& operator=(const ComPtr<T>& other) {
        return *this = other.ptr;
    }
    operator bool() const {
        return ptr != nullptr;
    }
private:
    T* ptr;
};