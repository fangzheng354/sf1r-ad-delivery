///
/// @file PropIdTable.h
/// @brief store property value ids for each doc
/// @author Jun Jiang <jun.jiang@izenesoft.com>
/// @date Created 2012-05-30
///

#ifndef SF1R_PROP_ID_TABLE_H
#define SF1R_PROP_ID_TABLE_H

#include "faceted_types.h"
#include "../MiningException.hpp"
#include <vector>
#include <boost/static_assert.hpp>
#include <boost/lexical_cast.hpp>

NS_FACETED_BEGIN

template <typename valueid_t, typename index_t>
struct PropIdTable
{
    PropIdTable();

    /// clear the table to empty.
    void clear();

    class PropIdList;

    void swap(PropIdTable& other);

    std::size_t size() const;

    void resize(std::size_t num);

    void getIdList(docid_t docId, PropIdList& propIdList) const;
    size_t getIdCount(docid_t docId) const;

    template <class IdContainer>
    void setIdList(docid_t docId, const IdContainer& idContainer);

    /// key: doc id
    /// value: if the most significant bit is 0, it's just the single value id
    ///        for the doc, otherwise, the following bits give the index in @c multiValueTable_.
    std::vector<index_t> indexTable_;

    /// key: index
    /// value: the count of value ids for the doc,
    ///        the following values are each value id for the doc.
    std::vector<valueid_t> multiValueTable_;

    /// the most significant bit for index type
    static const index_t INDEX_MSB = index_t(1) << (sizeof(index_t)*8 - 1);

    /// the mask to get the following bits for index type
    static const index_t INDEX_MASK = ~INDEX_MSB;

private:
    /// @see the requirement on parameter type size by #indexTable_
    BOOST_STATIC_ASSERT(sizeof(valueid_t) <= sizeof(index_t));
};

template <typename valueid_t, typename index_t>
class PropIdTable<valueid_t, index_t>::PropIdList
{
public:
    PropIdList() : size_(0), singleValueId_(0) {}

    std::size_t size() const { return size_; }

    bool empty() const { return size_ == 0; }

    valueid_t operator[](std::size_t i) const
    {
        if (i >= size_)
            return 0;

        return (size_ == 1) ? singleValueId_ : multiValueIter_[i];
    }

    void clear()
    {
        size_ = 0;
        singleValueId_ = 0;
    }

private:
    /// count of value ids
    valueid_t size_;

    /// when there is only one value id
    valueid_t singleValueId_;

    /// when there are multiple value ids, the iterator to the first value id
    typename std::vector<valueid_t>::const_iterator multiValueIter_;

    friend struct PropIdTable<valueid_t, index_t>;
};

template <typename valueid_t, typename index_t>
PropIdTable<valueid_t, index_t>::PropIdTable()
    : indexTable_(1) // doc id 0 is reserved for an empty doc
{
}

template <typename valueid_t, typename index_t>
void PropIdTable<valueid_t, index_t>::clear()
{
    indexTable_.resize(1);
    multiValueTable_.clear();
}

template <typename valueid_t, typename index_t>
void PropIdTable<valueid_t, index_t>::swap(PropIdTable& other)
{
    indexTable_.swap(other.indexTable_);
    multiValueTable_.swap(other.multiValueTable_);
}

template <typename valueid_t, typename index_t>
std::size_t PropIdTable<valueid_t, index_t>::size() const
{
    return indexTable_.size();
}

template <typename valueid_t, typename index_t>
void PropIdTable<valueid_t, index_t>::resize(std::size_t num)
{
    indexTable_.resize(num);
}

template <typename valueid_t, typename index_t>
void PropIdTable<valueid_t, index_t>::getIdList(docid_t docId, PropIdList& propIdList) const
{
    propIdList.clear();

    if (docId >= indexTable_.size())
        return;

    index_t index = indexTable_[docId];

    if (index & INDEX_MSB)
    {
        index &= INDEX_MASK;
        typename std::vector<valueid_t>::const_iterator it =
            multiValueTable_.begin() + index;

        propIdList.size_ = *it;
        propIdList.multiValueIter_ = ++it;
    }
    else if (index)
    {
        propIdList.size_ = 1;
        propIdList.singleValueId_ = index;
    }
}

template <typename valueid_t, typename index_t>
size_t PropIdTable<valueid_t, index_t>::getIdCount(docid_t docId) const
{
    if (docId >= indexTable_.size())
        return 0;

    index_t index = indexTable_[docId];

    if (index & INDEX_MSB)
    {
        return multiValueTable_[index & INDEX_MASK];
    }
    else
    {
        return index ? 1 : 0;
    }
}

template <typename valueid_t, typename index_t>
template <class IdContainer>
void PropIdTable<valueid_t, index_t>::setIdList(docid_t docId, const IdContainer& idContainer)
{
    if (docId >= indexTable_.size())
    {
        indexTable_.resize(docId + 1);
    }

    index_t& index = indexTable_[docId];
    const std::size_t inputNum = idContainer.size();

    switch (inputNum)
    {
        case 0:
        {
            index = 0;
            break;
        }

        case 1:
        {
            valueid_t inputId = *idContainer.begin();

            if (inputId >= INDEX_MSB)
            {
                throw MiningException("property value id is out of range",
                    boost::lexical_cast<std::string>(inputId),
                    "PropIdTable::setIdList");
            }

            index = inputId;
            break;
        }

        default:
        {
            std::size_t valueTableSize = multiValueTable_.size();

            if (valueTableSize >= INDEX_MSB)
            {
                throw MiningException("property value count is out of range",
                    boost::lexical_cast<std::string>(valueTableSize),
                    "PropIdTable::setIdList");
            }

            index = INDEX_MSB | valueTableSize;
            multiValueTable_.push_back(inputNum);
            multiValueTable_.insert(multiValueTable_.end(),
                idContainer.begin(), idContainer.end());
            break;
        }
    }
}

NS_FACETED_END

#endif // SF1R_PROP_ID_TABLE_H
