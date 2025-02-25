#pragma once

#include <Columns/ColumnVectorHelper.h>

#include <cmath>


namespace DB
{
/** Stuff for comparing numbers.
  * Integer values are compared as usual.
  * Floating-point numbers are compared this way that NaNs always end up at the end
  *  (if you don't do this, the sort would not work at all).
  */
template <typename T>
struct CompareHelper
{
    static bool less(T a, T b, int /*nan_direction_hint*/) { return a < b; }
    static bool greater(T a, T b, int /*nan_direction_hint*/) { return a > b; }

    /** Compares two numbers. Returns a number less than zero, equal to zero, or greater than zero if a < b, a == b, a > b, respectively.
      * If one of the values is NaN, then
      * - if nan_direction_hint == -1 - NaN are considered less than all numbers;
      * - if nan_direction_hint == 1 - NaN are considered to be larger than all numbers;
      * Essentially: nan_direction_hint == -1 says that the comparison is for sorting in descending order.
      */
    static int compare(T a, T b, int /*nan_direction_hint*/)
    {
        return a > b ? 1 : (a < b ? -1 : 0);
    }
};

template <typename T>
struct FloatCompareHelper
{
    static bool less(T a, T b, int nan_direction_hint)
    {
        bool isnan_a = std::isnan(a);
        bool isnan_b = std::isnan(b);

        if (isnan_a && isnan_b)
            return false;
        if (isnan_a)
            return nan_direction_hint < 0;
        if (isnan_b)
            return nan_direction_hint > 0;

        return a < b;
    }

    static bool greater(T a, T b, int nan_direction_hint)
    {
        bool isnan_a = std::isnan(a);
        bool isnan_b = std::isnan(b);

        if (isnan_a && isnan_b)
            return false;
        if (isnan_a)
            return nan_direction_hint > 0;
        if (isnan_b)
            return nan_direction_hint < 0;

        return a > b;
    }

    static int compare(T a, T b, int nan_direction_hint)
    {
        bool isnan_a = std::isnan(a);
        bool isnan_b = std::isnan(b);
        if (unlikely(isnan_a || isnan_b))
        {
            if (isnan_a && isnan_b)
                return 0;

            return isnan_a
                ? nan_direction_hint
                : -nan_direction_hint;
        }

        return (T(0) < (a - b)) - ((a - b) < T(0));
    }
};

template <>
struct CompareHelper<Float32> : public FloatCompareHelper<Float32>
{
};
template <>
struct CompareHelper<Float64> : public FloatCompareHelper<Float64>
{
};


/** To implement `get64` function.
  */
template <typename T>
inline UInt64 unionCastToUInt64(T x)
{
    return x;
}

template <>
inline UInt64 unionCastToUInt64(Float64 x)
{
    union
    {
        Float64 src;
        UInt64 res;
    };

    src = x;
    return res;
}

template <>
inline UInt64 unionCastToUInt64(Float32 x)
{
    union
    {
        Float32 src;
        UInt64 res;
    };

    res = 0;
    src = x;
    return res;
}


/** A template for columns that use a simple array to store.
  */
template <typename T>
class ColumnVector final : public COWPtrHelper<ColumnVectorHelper, ColumnVector<T>>
{
    static_assert(!IsDecimal<T>);

private:
    friend class COWPtrHelper<ColumnVectorHelper, ColumnVector<T>>;

    using Self = ColumnVector<T>;

    struct less;
    struct greater;

public:
    using value_type = T;
    using Container = PaddedPODArray<value_type>;

private:
    ColumnVector() = default;
    explicit ColumnVector(const size_t n)
        : data(n)
    {}
    ColumnVector(const size_t n, const value_type x)
        : data(n, x)
    {}
    ColumnVector(const ColumnVector & src)
        : data(src.data.begin(), src.data.end()){};

    /// Sugar constructor.
    ColumnVector(std::initializer_list<T> il)
        : data{il}
    {}

public:
    bool isNumeric() const override { return is_arithmetic_v<T>; }

    size_t size() const override
    {
        return data.size();
    }

    StringRef getDataAt(size_t n) const override
    {
        return StringRef(reinterpret_cast<const char *>(&data[n]), sizeof(data[n]));
    }

    void insertFrom(const IColumn & src, size_t n) override
    {
        data.push_back(static_cast<const Self &>(src).getData()[n]);
    }

    void insertData(const char * pos, size_t /*length*/)
        override
    {
        data.push_back(*reinterpret_cast<const T *>(pos));
    }

    void insertDefault() override
    {
        data.push_back(T());
    }

    void popBack(size_t n) override
    {
        data.resize_assume_reserved(data.size() - n);
    }

    StringRef serializeValueIntoArena(size_t n, Arena & arena, char const *& begin, const TiDB::TiDBCollatorPtr &, String &) const override;

    const char * deserializeAndInsertFromArena(const char * pos, const TiDB::TiDBCollatorPtr &) override;

    void updateHashWithValue(size_t n, SipHash & hash, const TiDB::TiDBCollatorPtr &, String &) const override;
    void updateHashWithValues(IColumn::HashValues & hash_values, const TiDB::TiDBCollatorPtr &, String &) const override;
    void updateWeakHash32(WeakHash32 & hash, const TiDB::TiDBCollatorPtr &, String &) const override;

    size_t byteSize() const override
    {
        return data.size() * sizeof(data[0]);
    }

    size_t byteSize(size_t /*offset*/, size_t limit) const override
    {
        return limit * sizeof(data[0]);
    }

    size_t allocatedBytes() const override
    {
        return data.allocated_bytes();
    }

    void insert(const T value)
    {
        data.push_back(value);
    }

    /// This method implemented in header because it could be possibly devirtualized.
    int compareAt(size_t n, size_t m, const IColumn & rhs_, int nan_direction_hint) const override
    {
        return CompareHelper<T>::compare(data[n], static_cast<const Self &>(rhs_).data[m], nan_direction_hint);
    }

    void getPermutation(bool reverse, size_t limit, int nan_direction_hint, IColumn::Permutation & res) const override;

    void reserve(size_t n) override
    {
        data.reserve(n);
    }

    const char * getFamilyName() const override;

    MutableColumnPtr cloneResized(size_t size) const override;

    Field operator[](size_t n) const override
    {
        return typename NearestFieldType<T>::Type(data[n]);
    }

    void get(size_t n, Field & res) const override
    {
        res = typename NearestFieldType<T>::Type(data[n]);
    }

    UInt64 get64(size_t n) const override;

    UInt64 getUInt(size_t n) const override;

    Int64 getInt(size_t n) const override;

    void insert(const Field & x) override
    {
        data.push_back(DB::get<typename NearestFieldType<T>::Type>(x));
    }

    void insertRangeFrom(const IColumn & src, size_t start, size_t length) override;

    ColumnPtr filter(const IColumn::Filter & filt, ssize_t result_size_hint) const override;

    ColumnPtr permute(const IColumn::Permutation & perm, size_t limit) const override;

    ColumnPtr replicate(const IColumn::Offsets & offsets) const override;

    void getExtremes(Field & min, Field & max) const override;

    MutableColumns scatter(IColumn::ColumnIndex num_columns, const IColumn::Selector & selector) const override
    {
        return this->template scatterImpl<Self>(num_columns, selector);
    }

    void gather(ColumnGathererStream & gatherer_stream) override;

    bool canBeInsideNullable() const override { return true; }

    bool isFixedAndContiguous() const override { return true; }
    size_t sizeOfValueIfFixed() const override { return sizeof(T); }

    StringRef getRawData() const override
    {
        return StringRef(reinterpret_cast<const char *>(data.data()), byteSize());
    }

    /** More efficient methods of manipulation - to manipulate with data directly. */
    Container & getData()
    {
        return data;
    }

    const Container & getData() const
    {
        return data;
    }

    const T & getElement(size_t n) const
    {
        return data[n];
    }

    T & getElement(size_t n)
    {
        return data[n];
    }

protected:
    Container data;
};


} // namespace DB
