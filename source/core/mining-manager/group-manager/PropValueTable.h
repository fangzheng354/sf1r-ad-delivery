///
/// @file PropValueTable.h
/// @brief a table contains below things for a specific property:
///        1. mapping between property UString value and id
///        2. mapping from doc id to property value id
/// @author Jun Jiang <jun.jiang@izenesoft.com>
/// @date Created 2011-03-24
///

#ifndef SF1R_PROP_VALUE_TABLE_H_
#define SF1R_PROP_VALUE_TABLE_H_

#include <common/inttypes.h>
#include <common/PropSharedLock.h>
#include "PropIdTable.h"
#include "faceted_types.h"

#include <util/ustring/UString.h>

#include <3rdparty/am/btree/btree_set.h>
#include <vector>
#include <string>
#include <map>
#include <set>

NS_FACETED_BEGIN

class PropValueTable : public PropSharedLock
{
public:
    /**
     * property value id type.
     * as 0 is reserved as invalid id, meaning no property value is availabe,
     * the valid id range is [1, 2^16) (65535 ids)
     */
    typedef uint32_t pvid_t;

    typedef PropIdTable<pvid_t, uint32_t> ValueIdTable;
    typedef ValueIdTable::PropIdList PropIdList;

    /** mapping from value string to value id */
    typedef std::map<izenelib::util::UString, pvid_t> PropStrMap;
    /** mapping from value id to the map of child values */
    typedef std::vector<PropStrMap> ChildMapTable;

    /** mapping from value id to parent value id */
    typedef std::vector<pvid_t> ParentIdTable;

    typedef btree::btree_set<pvid_t> ParentSetType;
    //typedef std::set<pvid_t> ParentSetType;

    PropValueTable();
    PropValueTable(const std::string& dirPath, const std::string& propName);
    PropValueTable(const PropValueTable& table);

    PropValueTable& operator=(const PropValueTable& other);
    void swap(PropValueTable& other);

    bool open();
    bool flush();

    /**
     * Clear the table to empty.
     */
    void clear();

    const std::string& dirPath() const { return dirPath_; }

    const std::string& propName() const { return propName_; }

    std::size_t docIdNum() const { return valueIdTable_.size(); }

    void resize(std::size_t num);

    void setPropIdList(docid_t docId, const std::vector<pvid_t>& inputIdList);

    std::size_t propValueNum() const { return propStrVec_.size(); }

    void propValueStr(
        pvid_t pvId,
        izenelib::util::UString& ustr,
        bool isLock = true) const;

    /**
     * Insert property value id.
     * @param path the path of property value, from root node to leaf node
     * @exception MiningException when the new id created is overflow
     * @return value id, if @p path is not inserted before, its new id is created and returned
     */
    pvid_t insertPropValueId(const std::vector<izenelib::util::UString>& path);

    /**
     * Get property value id.
     * @param path the path of property value, from root node to leaf node
     * @param isLock whether need to create a read lock, if the caller has
     * already created one, this parameter should be false to avoid
     * duplicate lock.
     * @return value id, if @p path is not inserted before, 0 is returned
     */
    pvid_t propValueId(
        const std::vector<izenelib::util::UString>& path,
        bool isLock = true) const;

    /**
     * Given value id @p pvId, get its path from root node to leaf node.
     * @param pvId the value id
     * @param path store the path
     * @param isLock whether need to create a read lock, if the caller has
     * already created one, this parameter should be false to avoid
     * duplicate lock.
     */
    void propValuePath(
        pvid_t pvId,
        std::vector<izenelib::util::UString>& path,
        bool isLock = true) const;

    /**
     * @attention before calling below public functions,
     * you must call this statement for safe concurrent access:
     *
     * <code>
     * PropValueTable::ScopedReadLock lock(PropValueTable::getMutex());
     * </code>
     */
    void getPropIdList(docid_t docId, PropIdList& propIdList) const
    {
        valueIdTable_.getIdList(docId, propIdList);
    }

    const ChildMapTable& childMapTable() const { return childMapTable_; }

    const ParentIdTable& parentIdTable() const { return parentIdVec_; }

    /**
     * Get the first property value id for @p docId.
     * @return the first value id, if @p docId has no value ids, 0 is returned.
     */
    pvid_t getFirstValueId(docid_t docId) const;

    /**
     * Get the parent ids of @p pvId.
     * For example, assume @p pvId is 100, 100's parent id is 10,
     * 10's parent id is 1, and 1 is a root id,
     * then @p parentIds would become [100, 10, 1].
     */
    void getParentIds(pvid_t pvId, std::vector<pvid_t>& parentIds) const;

    /**
     * Get the root id of @p pvId.
     * For example, assume @p pvId is 100, 100's parent id is 10,
     * 10's parent id is 1, and 1 is a root id,
     * then it would return 1.
     */
    pvid_t getRootValueId(pvid_t pvId) const;

    /**
     * Whether @p docId belongs to group label of @p labelId.
     * @param docId the doc id
     * @param labelId the property value id of group label
     * @return true for belongs to, false for not belongs to.
     */
    bool testDoc(docid_t docId, pvid_t labelId) const;

    /**
     * Get value ids of @p docId, including all its parent ids.
     * @param docId the doc id
     * @param parentSet the set of value ids
     * TODO When boost::memory has a TLS allocator, the tempte could be removed by 
     * assigning a ParentSetType as a member variable of class PropValueTable
     */
    template<typename SetType>
    void parentIdSet(docid_t docId, SetType& parentSet) const;

private:
    /**
     * Save each property value and its parent id to text file for debug use.
     * @param dirPath directory path
     * @param fileName file name
     * @return true for success, false for failure
     */
    bool saveParentId_(const std::string& dirPath, const std::string& fileName) const;

private:
    /** directory path */
    std::string dirPath_;

    /** property name */
    std::string propName_;

    /** mapping from value id to value string */
    std::vector<izenelib::util::UString> propStrVec_;
    /** the number of elements in @c propStrVec_ saved in file */
    unsigned int savePropStrNum_;

    /** mapping from value id to parent value id */
    ParentIdTable parentIdVec_;
    /** the number of elements in @c parentIdVec_ saved in file */
    unsigned int saveParentIdNum_;

    /** mapping from value id to the map of child values */
    ChildMapTable childMapTable_;

    /** mapping from doc id to a list of property value ids */
    ValueIdTable valueIdTable_;
    /** the number of elements in @c valueIdTable_.indexTable_ saved in file */
    unsigned int saveIndexNum_;
    /** the number of elements in @c valueIdTable_.multiValueTable_ saved in file */
    unsigned int saveValueNum_;
};

template<typename SetType>
void PropValueTable::parentIdSet(docid_t docId, SetType& parentSet) const
{
    PropIdList propIdList;
    getPropIdList(docId, propIdList);

    const std::size_t idNum = propIdList.size();
    for (std::size_t i = 0; i < idNum; ++i)
    {
        for (pvid_t pvId = propIdList[i]; pvId; pvId = parentIdVec_[pvId])
        {
            // stop finding parent if already inserted
            if (parentSet.insert(pvId).second == false)
                break;
        }
    }
}

NS_FACETED_END

#endif
