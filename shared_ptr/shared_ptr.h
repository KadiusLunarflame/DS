#include <iostream>
#include <cstddef>
#include <memory>
#include <new>
#include <typeinfo>


template<typename T>
class shared_ptr;

template<typename T>
class weak_ptr;

namespace smart_ptr {

template<typename T, typename Alloc, typename... Args>
shared_ptr<T> allocate_shared(const Alloc& alloc, Args&&... args) {
    return shared_ptr<T>(alloc, std::forward<Args>(args)...);
}


template<typename T, typename... Args>
shared_ptr<T> make_shared(Args&&... args) {
    return shared_ptr<T>(std::allocator<T>(), std::forward<Args>(args)...);
}

template <typename T, typename Alloc>
struct DefaultDeleter {
    Alloc alloc;
    DefaultDeleter(const Alloc& alloc): alloc(alloc) {}

    void operator()(T* ptr) {
        std::allocator_traits<Alloc>::destroy(alloc, ptr);
    }
};

struct CB_BASE {

    virtual ~CB_BASE(){}
    virtual void operator()(void* ptr) = 0;

    virtual void SharedCountInc() = 0;
    virtual void SharedCountDec() = 0;
    virtual size_t GetSharedCount() = 0;

    virtual void WeakCountInc() = 0;
    virtual void WeakCountDec() = 0;
    virtual size_t GetWeakCount() = 0;

    virtual void destroy() = 0;

};




template <typename T, typename Alloc, typename Deleter>
struct ControlBlock: public smart_ptr::CB_BASE {
    using ControlBlockType = ControlBlock<T, Alloc, Deleter>;
    using ControlDataType = ControlData<T, Alloc, Deleter>;

    size_t shared_count;
    size_t weak_count;
    Alloc alloc;
    Deleter deleter;

    void SharedCountInc() override;
    void SharedCountDec() override;
    size_t GetSharedCount() override;

    void WeakCountInc() override;
    void WeakCountDec() override;
    size_t GetWeakCount() override;
    //type erasure idiom
    /* Не знаем какой тип T подсунет пользователь, поэтому стираем тим: принимаем void* и кастим к Т */
    //void operator(T* ptr) override{/* void* ptr ??? */
    //    deleter(ptr);/* reinterpret_cast<T*> ??? */
    //}

    void operator()(void* ptr) override{
        deleter(static_cast<T*>(ptr)); //reinterpret_cast?
    }

    void destroy() override {

        //if ControlBlock was created via make_shared or allocate_shared
        if(std::is_same_v<Deleter, smart_ptr::DefaultDeleter<T, Alloc>>) {
            this->~ControlBlock();

            using AllocDataType = typename std::allocator_traits<Alloc>:: template rebind_alloc<ControlDataType>; 
            AllocDataType alloc_cd;

            std::allocator_traits<AllocDataType>::deallocate(alloc_cd, reinterpret_cast<ControlDataType*>(this), 1);
            return;
        }

        this->~ControlBlock();
        using AllocControlBlockType = typename std::allocator_traits<Alloc>:: template rebind_alloc<ControlBlockType>;
        AllocControlBlockType alloc_cb;
        
        std::allocator_traits<AllocControlBlockType>::deallocate(alloc_cb, this, 1);

    }


    /*  Если Deleter - дефолтный делитер (std::allocator_traits<std::allocator<T>>::destroy()), тогда необходимо уничтожить
        и ControlBlock, и сам объект
        Если Deleter  - нетривиальный, то пользователь вполне вероятно не хочет уничтожать объект, а сделать что-то другое.
        Например, закрыть файл или выключить сетевое соединение.
        => следует уничтожить только ControlBlock и вызвать перегруженный operator() на объект.

        У нас два варианта развития событий:
            1) Пользователь создает shared_ptr с помощью функции make_shared(Args...)
                Тогда у пользователя нет возможности задать нестандартный делитер.
            2) Пользователь создает shared_ptr из сишного указателя. Только в этом случае у пользователя есть возможность
                задать нестандартный делитер.

        В первом случае удобно обернуть ControlBlock и object в другую структуру и в зависимости от типа делитера уничтожать
        или эту структуру целиком(reinterpret_cast к внешней структуре), или же уничтожать ControlBlock и object поотдельности

        Деструктор ControlBlock тривиален, гораздо интереснее вопрос деаллокации. */ 
    ControlBlock(Alloc a = Alloc(), Deleter d = Deleter()): shared_count(1), weak_count(0), alloc(a), deleter(d)
    {}

    ~ControlBlock() = default;
};

template <typename T, typename Alloc, typename Deleter>
void ControlBlock<T, Alloc, Deleter>::SharedCountInc() {
    ++shared_count;
}

template <typename T, typename Alloc, typename Deleter>
void ControlBlock<T, Alloc, Deleter>::SharedCountDec() {
    --shared_count;
}

template <typename T, typename Alloc, typename Deleter>
size_t ControlBlock<T, Alloc, Deleter>::GetSharedCount() {
    return shared_count;
}

template <typename T, typename Alloc, typename Deleter>
void ControlBlock<T, Alloc, Deleter>::WeakCountInc() {
    ++weak_count;
}

template <typename T, typename Alloc, typename Deleter>
void ControlBlock<T, Alloc, Deleter>::WeakCountDec() {
    --weak_count;
}

template <typename T, typename Alloc, typename Deleter>
size_t ControlBlock<T, Alloc, Deleter>::GetWeakCount() {
    return weak_count;
}





template<typename T, typename Alloc, typename Deleter>
struct ControlData {
    ControlBlock<T, Alloc, Deleter> counter;
    T object;
};




}



template<typename T, typename Alloc, typename Deleter>
struct ControlData;



template<typename T>
class shared_ptr {
private:
    template<typename U>
    friend class shared_ptr;

    template<typename U>
    friend class weak_ptr;

    //WE do not know the exact type of ControlBlock, so we type erase its type
    smart_ptr::CB_BASE* counter;
    T* ptr;//указатель на объект
    

    //private constructor for make/allocate_shared => with default deleter and std::allocator/custom allocator
    template<typename Alloc, typename... Args>
    shared_ptr(Alloc alloc, Args&&... args);



public:
    template<typename U, typename... Args>
    friend shared_ptr<U> smart_ptr::make_shared(Args&&... args);

    template<typename U, typename Alloc, typename... Args>
    friend shared_ptr<U> smart_ptr::allocate_shared(const Alloc& alloc, Args&&... args);

    shared_ptr(); //(1)  constexpr noexcept
    shared_ptr(std::nullptr_t); //(2)  constexpr noexcept

    template<typename U>
    explicit shared_ptr(U* ptr); //(3) 

    template<typename U, typename Deleter>
    shared_ptr(U* ptr, Deleter d); //(4)

    //template<> 

    template<typename U, typename Deleter, typename Alloc>
    shared_ptr(U* ptr, Deleter d, Alloc alloc);//(6)

    // template<typename Deleter, typename Alloc>
    // shared_ptr(std::nullptr_t, Deleter d, Alloc alloc); //(7)

    shared_ptr(const shared_ptr<T>& another); //(9) noexcept

    template<typename U>
    shared_ptr(const shared_ptr<U>& another); //(9) noexcept

    shared_ptr(shared_ptr&& another); //(10)  noexcept

    template<typename U>
    shared_ptr(shared_ptr<U>&& another); //(10)  noexcept

    template<typename U>
    explicit shared_ptr(const weak_ptr<U>& another);

    void swap(shared_ptr& rhs) {
        std::swap(counter, rhs.counter);
        std::swap(ptr, rhs.ptr);
    }

    shared_ptr& operator=(const shared_ptr& rhs) noexcept{
        //what if counter == nullptr?
        //copy-and-swap
        shared_ptr(rhs).swap(*this);
    }

    shared_ptr& operator=(shared_ptr&& rhs) noexcept{
        shared_ptr(std::move<T>(rhs)).swap(*this);
    }

    T& operator*() const noexcept{
        return *(ptr);
    }

    T* operator->() const noexcept{
        return ptr;
    }

    T* get() {
        return ptr;
    }

    size_t use_count() {
        return counter->GetSharedCount();
    }

    size_t weak_count() {
        return counter->GetWeakCount();
    }

    explicit operator bool(){
        return get() == nullptr? false: true;
    }

    ~shared_ptr();

};

template<typename T>
/*constexpr*/ shared_ptr<T>::shared_ptr(): counter(nullptr), ptr(nullptr) {}

template<typename T>
shared_ptr<T>::shared_ptr(std::nullptr_t): counter(nullptr), ptr(nullptr) {}

template<typename T>
template<typename U>                                //DefaultDeleter without allocator is std::default_delete<U>()
shared_ptr<T>::shared_ptr(U* ptr): shared_ptr(ptr, std::default_delete<U>(), std::allocator<U>())/*ptr(ptr)*/ {
    //create a ControlBlock with std::allocator<U> and DefaultDeleter<U, std::allocator<U>>
}

template<typename T>
template<typename U, typename Deleter>
shared_ptr<T>::shared_ptr(U* ptr, Deleter d): shared_ptr(ptr, d, std::allocator<U>()) {
    //create a ControlBlock with std::allocator<U> and custom Deleter
}

template<typename T>
template<typename U, typename Deleter, typename Alloc>
shared_ptr<T>::shared_ptr(U* ptr, Deleter d, Alloc alloc): ptr(ptr) {
    //create a COntrolBlock with custom allocator and deleter
    using ControlBlockType = ControlBlock<U, Alloc, Deleter>;
    using AllocType = typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockType>;//allocator for ControlBlock

    AllocType CBAlloc;
    counter = std::allocator_traits<AllocType>::allocate(CBAlloc, 1);
    std::allocator_traits<AllocType>::construct(CBAlloc, counter, alloc, d);
}

template<typename T>
shared_ptr<T>::shared_ptr(const shared_ptr<T>& another): counter(another.counter), ptr(another.ptr) {
    counter->SharedCountInc();
}

template<typename T>
template<typename U>
shared_ptr<T>::shared_ptr(const shared_ptr<U>& another): counter(another.counter), ptr(another.ptr) {
    counter->SharedCountInc();
}

template<typename T>
shared_ptr<T>::shared_ptr(shared_ptr<T>&& another): counter(another.counter), ptr(another.ptr) {
    another.counter = nullptr;
    ptr = nullptr;
}

template<typename T>
template<typename U>
shared_ptr<T>::shared_ptr(shared_ptr<U>&& another): counter(another.counter), ptr(another.ptr) {
    another.counter = nullptr;
    ptr = nullptr;
}

template<typename T>
shared_ptr<T>::~shared_ptr() {
    counter->SharedCountDec();

    if(counter->GetSharedCount() == 0 && counter->GetWeakCount() == 0) {
        //No more shared_ptrs and weak_ptrs
        //destroy ControlData or destroy object and ControlBlock separately
        //1st branch: destroy ControlData
        /*  this happens only if ControlData->ControlBlock->Deleter is same as DefaultDeleter  */
        //2nd branch: call delete on ptr, destroy and then deallocate ControlBlock;

        //Had to wrap the branches above into virtual destroy method

        //No way to do the following cuz AllocType and DeleterType are type erased
        // using AllocType = decltype(counter->alloc);
        // using DeleterType = decltype(counter->deleter);
        counter->operator()(ptr);//called deleter(ptr)
        counter->destroy();
        return;
    }

    if(counter->GetSharedCount() == 0 && counter->GetWeakCount() > 0) {
        //No more shared_ptrs, but there are some weak_ptrs still. In this case we destroy the object only
        counter->operator()(ptr);
    }
}


template<typename T>
template<typename U>
shared_ptr<T>::shared_ptr(const weak_ptr<U>& another): ptr(ptr), counter(counter) {
    counter->SharedCountInc();
}

template<typename T>
class weak_ptr {
    template<typename U>
    friend class shared_ptr;

    template<typename U>
    friend class weak_ptr;

    smart_ptr::CB_BASE* counter;
    T* ptr;

public:

    constexpr weak_ptr();

    weak_ptr(const weak_ptr& another);

    template<typename U>
    weak_ptr(const weak_ptr<U>& another);

    template<typename U>
    weak_ptr(const shared_ptr<U>& another);

    weak_ptr(weak_ptr&& another);

    template<typename U>
    weak_ptr(weak_ptr<U>&& another);

    size_t use_count() {
        return counter->GetSharedCount();
    }

    size_t weak_count() {
        return counter->GetWeakCount();
    }

    bool expired() {
        return counter->GetSharedCount() == 0;
    }

    shared_ptr<T> lock() {
        return shared_ptr<T>::shared_count(*this);
    }

    void swap(weak_ptr& another) {
        std::swap(counter, another.counter);
        std::swap(ptr, another.ptr);
    }

    weak_ptr& operator=(const weak_ptr& rhs) {
        weak_ptr<T>(rhs).swap(*this);

        return *this;
    }

    template<typename U>
    weak_ptr& operator=(const weak_ptr<U>& rhs) {
        weak_ptr<T>(rhs).swap(*this);

        return *this;
    }

    weak_ptr& operator=(weak_ptr&& rhs) {
        weak_ptr<T>(std::move(rhs)).swap(*this);

        return *this;
    }

    template<typename U>
    weak_ptr& operator=(weak_ptr<U>&& rhs) {
        weak_ptr<T>(std::move(rhs)).swap(*this);

        return *this;
    }

    ~weak_ptr();

    weak_ptr(const shared_ptr<T>& another);


};

template<typename T>
constexpr weak_ptr<T>::weak_ptr(): ptr(nullptr), counter(nullptr)
{}

template<typename T>
weak_ptr<T>::weak_ptr(const weak_ptr& another): ptr(another.ptr), counter(another.counter) {
    counter->WeakCountInc();
}

template<typename T>
template<typename U>
weak_ptr<T>::weak_ptr(const weak_ptr<U>& another): ptr(another.ptr), counter(another.counter) {
    counter->WeakCountInc();
}

template<typename T>
weak_ptr<T>::weak_ptr(weak_ptr&& another): counter(another.counter), ptr(another.ptr) {
    another.counter = nullptr;
    another.ptr = nullptr;
}

template<typename T>
template<typename U>
weak_ptr<T>::weak_ptr(weak_ptr<U>&& another): counter(another.counter), ptr(ptr) {
    another.counter = nullptr;
    another.ptr = nullptr;
}

template<typename T>
weak_ptr<T>::~weak_ptr() {
    counter->WeakCountDec();
    
    if(counter->GetSharedCount() == 0 && counter->GetWeakCount() == 0) {
        counter->destroy();
    }
}

template<typename T>
weak_ptr<T>::weak_ptr(const shared_ptr<T>& another): ptr(another.ptr), counter(another.counter)  {
    counter->WeakCountInc();
}

template<typename T>
template<typename U>
weak_ptr<T>::weak_ptr(const shared_ptr<U>& another): ptr(another.ptr), counter(another.counter)  {
    counter->WeakCountInc();
}



template<typename T>
template<typename Alloc, typename... Args>
shared_ptr<T>::shared_ptr(Alloc alloc, Args&&... args) {
    //Allocate ControlData and alloc_traits::construct ControlBlock and Object

    using ControlBlockType = ControlBlock<T, Alloc, smart_ptr::DefaultDeleter<T, Alloc>>;
    using ControlDataType = ControlData<T, Alloc, smart_ptr::DefaultDeleter<T, Alloc>>;

    using AllocDataType = typename std::allocator_traits<Alloc>::template rebind_alloc<ControlDataType>;

    //Alloc alloc -> alocator for T 

    AllocDataType DataAlloc;//allocator for ControlData
    auto* NewControlData = std::allocator_traits<AllocDataType>::allocate(DataAlloc, 1);
    /* Allocated ControlData */

    counter = &(NewControlData->counter);
    ptr = &(NewControlData->object);

    std::allocator_traits<Alloc>::construct(alloc, ptr, std::forward<Args>(args)...);

    //new (counter) ControlBlockType(alloc, DefaultDeleter<T, Alloc>(alloc));

    //allocator for ControlBlock
    using AllocControlBlockType = typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockType>;
    AllocControlBlockType CBAlloc;

    std::cout << typeid(counter).name() << std::endl;
    auto* c = static_cast<ControlBlockType*>(counter);
    std::allocator_traits<AllocControlBlockType>::construct(CBAlloc, c, alloc, smart_ptr::DefaultDeleter<T, Alloc>(alloc));
}