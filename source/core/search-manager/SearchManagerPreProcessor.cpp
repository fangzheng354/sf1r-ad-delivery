#include "SearchManagerPreProcessor.h"
#include "Sorter.h"
#include "NumericPropertyTableBuilder.h"
#include "RTypeStringPropTableBuilder.h"
#include <common/RTypeStringPropTable.h>
#include <common/PropSharedLockSet.h>
#include "DocumentIterator.h"
#include <ranking-manager/RankQueryProperty.h>
#include <ranking-manager/PropertyRanker.h>
#include <mining-manager/product-scorer/ProductScorerFactory.h>
#include <mining-manager/product-scorer/ProductScoreParam.h>
#include <common/SFLogger.h>
#include <util/get.h>
#include <util/ClockTimer.h>
#include <fstream>
#include <algorithm> // to_lower

using namespace sf1r;

const std::string RANK_PROPERTY("_rank");
const std::string DATE_PROPERTY("date");
const std::string CUSTOM_RANK_PROPERTY("custom_rank");

SearchManagerPreProcessor::SearchManagerPreProcessor(const IndexBundleSchema& indexSchema)
    : productScorerFactory_(NULL)
    , numericTableBuilder_(NULL)
    , rtypeStringPropTableBuilder_(NULL)
{
    for (IndexBundleSchema::const_iterator iter = indexSchema.begin();
         iter != indexSchema.end(); ++iter)
    {
        schemaMap_[iter->getName()] = *iter;
    }
}

void SearchManagerPreProcessor::prepareSorterCustomRanker(
    const SearchKeywordOperation& actionOperation,
    boost::shared_ptr<Sorter>& pSorter,
    CustomRankerPtr& customRanker)
{
    std::vector<std::pair<std::string, bool> >& sortPropertyList
        = actionOperation.actionItem_.sortPriorityList_;

    if (sortPropertyList.empty())
        return;

    if (sortPropertyList.size() == 1)
    {
        std::string name = sortPropertyList[0].first;
        boost::to_lower(name);

        bool isDescend = !sortPropertyList[0].second;
        if (name == RANK_PROPERTY && isDescend)
            return;
    }

    {
        std::vector<std::pair<std::string, bool> >::iterator iter = sortPropertyList.begin();
        for (; iter != sortPropertyList.end(); ++iter)
        {
            std::string fieldNameL = iter->first;
            boost::to_lower(fieldNameL);
            // sort by custom ranking
            if (fieldNameL == CUSTOM_RANK_PROPERTY)
            {
                // prepare custom ranker data, custom score will be evaluated later as rank score
                customRanker = actionOperation.actionItem_.customRanker_;
                if (!customRanker)
                    customRanker = buildCustomRanker_(actionOperation.actionItem_);
                if (!customRanker->setPropertyData(numericTableBuilder_))
                {
                    LOG(ERROR) << customRanker->getErrorInfo() ;
                    continue;
                }
                //customRanker->printESTree();

                if (!pSorter) pSorter.reset(new Sorter(numericTableBuilder_,rtypeStringPropTableBuilder_));
                SortProperty* pSortProperty = new SortProperty(
                    "CUSTOM_RANK",
                    CUSTOM_RANKING_PROPERTY_TYPE,
                    SortProperty::CUSTOM,
                    iter->second);
                pSorter->addSortProperty(pSortProperty);
                continue;
            }
            // sort by rank
            if (fieldNameL == RANK_PROPERTY)
            {
                if (!pSorter) pSorter.reset(new Sorter(numericTableBuilder_,rtypeStringPropTableBuilder_));
                SortProperty* pSortProperty = new SortProperty(
                    "RANK",
                    UNKNOWN_DATA_PROPERTY_TYPE,
                    SortProperty::SCORE,
                    iter->second);
                pSorter->addSortProperty(pSortProperty);
                continue;
            }
            // sort by date
            if (fieldNameL == DATE_PROPERTY)
            {
                if (!pSorter) pSorter.reset(new Sorter(numericTableBuilder_,rtypeStringPropTableBuilder_));
                SortProperty* pSortProperty = new SortProperty(
                    iter->first,
                    INT64_PROPERTY_TYPE,
                    iter->second);
                pSorter->addSortProperty(pSortProperty);
                continue;
            }

            // sort by arbitrary property
            SchemaMap::const_iterator it = schemaMap_.find(iter->first);
            if (it == schemaMap_.end())
                continue;

            const PropertyConfig& propertyConfig = it->second;
            if (!propertyConfig.isIndex() || propertyConfig.isAnalyzed())
                continue;

            LOG(INFO) << "add sort property : " << iter->first;

            PropertyDataType propertyType = propertyConfig.getType();
            switch (propertyType)
            {
            case STRING_PROPERTY_TYPE:
            case INT32_PROPERTY_TYPE:
            case FLOAT_PROPERTY_TYPE:
            case DATETIME_PROPERTY_TYPE:
            case INT8_PROPERTY_TYPE:
            case INT16_PROPERTY_TYPE:
            case INT64_PROPERTY_TYPE:
            case DOUBLE_PROPERTY_TYPE:
            case NOMINAL_PROPERTY_TYPE:
            {
                if (!pSorter) pSorter.reset(new Sorter(numericTableBuilder_,rtypeStringPropTableBuilder_));
                SortProperty* pSortProperty = new SortProperty(
                    iter->first,
                    propertyType,
                    iter->second);
                pSorter->addSortProperty(pSortProperty);
                break;
            }
            default:
                DLOG(ERROR) << "Sort by properties other than int, float, double type"; // TODO : Log
                break;
            }
        }
    }
}

bool SearchManagerPreProcessor::getPropertyTypeByName_(
    const std::string& name,
    PropertyDataType& type) const
{
    SchemaMap::const_iterator it = schemaMap_.find(name);

    if (it != schemaMap_.end())
    {
        type = it->second.getType();
        return true;
    }

    return false;
}

CustomRankerPtr
SearchManagerPreProcessor::buildCustomRanker_(KeywordSearchActionItem& actionItem)
{
    CustomRankerPtr customRanker(new CustomRanker());

    customRanker->getConstParamMap() = actionItem.paramConstValueMap_;
    customRanker->getPropertyParamMap() = actionItem.paramPropertyValueMap_;

    customRanker->parse(actionItem.strExp_);

    std::map<std::string, PropertyDataType>& propertyDataTypeMap
    = customRanker->getPropertyDataTypeMap();
    std::map<std::string, PropertyDataType>::iterator iter
    = propertyDataTypeMap.begin();
    for (; iter != propertyDataTypeMap.end(); iter++)
    {
        getPropertyTypeByName_(iter->first, iter->second);
    }

    return customRanker;
}

void SearchManagerPreProcessor::fillSearchInfoWithSortPropertyData(
    Sorter* pSorter,
    std::vector<unsigned int>& docIdList,
    DistKeywordSearchInfo& distSearchInfo,
    PropSharedLockSet& propSharedLockSet)
{
    if (!pSorter) return;

    size_t docNum = docIdList.size();
    std::list<SortProperty*>& sortProperties = pSorter->sortProperties_;
    std::list<SortProperty*>::iterator iter;
    SortProperty* pSortProperty;

    for (iter = sortProperties.begin(); iter != sortProperties.end(); ++iter)
    {
        pSortProperty = *iter;
        const std::string& sortPropertyName = pSortProperty->getProperty();
        distSearchInfo.sortPropertyList_.push_back(
            std::make_pair(sortPropertyName, pSortProperty->isReverse()));

        LOG(INFO) << "adding sort property : " << sortPropertyName;
        if (sortPropertyName == "CUSTOM_RANK" || sortPropertyName == "RANK")
            continue;

        if (pSortProperty->getPropertyDataType() == STRING_PROPERTY_TYPE)
        {
            if (!rtypeStringPropTableBuilder_)
                continue;
            boost::shared_ptr<RTypeStringPropTable>& strPropertyTable =
                rtypeStringPropTableBuilder_->createPropertyTable(sortPropertyName);
            if (!strPropertyTable)
                continue;
            propSharedLockSet.insertSharedLock(strPropertyTable.get());
            distSearchInfo.sortPropertyStrDataList_.push_back(std::make_pair(sortPropertyName, std::vector<std::string>()));
            std::vector<std::string>& dataList = distSearchInfo.sortPropertyStrDataList_.back().second;
            dataList.resize(docNum);
            for (size_t i = 0; i < docNum; ++i)
            {
                strPropertyTable->getRTypeString(docIdList[i], dataList[i]);
            }
            continue;
        }
        if (!numericTableBuilder_)
            continue;

        boost::shared_ptr<NumericPropertyTableBase>& numericPropertyTable =
            numericTableBuilder_->createPropertyTable(sortPropertyName);
        if (!numericPropertyTable)
            continue;

        propSharedLockSet.insertSharedLock(numericPropertyTable.get());
        switch (numericPropertyTable->getType())
        {
        case INT32_PROPERTY_TYPE:
        case INT8_PROPERTY_TYPE:
        case INT16_PROPERTY_TYPE:
        {
            distSearchInfo.sortPropertyInt32DataList_.push_back(std::make_pair(sortPropertyName, std::vector<int32_t>()));
            std::vector<int32_t>& dataList = distSearchInfo.sortPropertyInt32DataList_.back().second;
            dataList.resize(docNum);
            for (size_t i = 0; i < docNum; i++)
            {
                numericPropertyTable->getInt32Value(docIdList[i], dataList[i], false);
            }
        }
        break;
        case INT64_PROPERTY_TYPE:
        case DATETIME_PROPERTY_TYPE:
        {
            distSearchInfo.sortPropertyInt64DataList_.push_back(std::make_pair(sortPropertyName, std::vector<int64_t>()));
            std::vector<int64_t>& dataList = distSearchInfo.sortPropertyInt64DataList_.back().second;
            dataList.resize(docNum);
            for (size_t i = 0; i < docNum; i++)
            {
                numericPropertyTable->getInt64Value(docIdList[i], dataList[i], false);
            }
        }
        break;
        case FLOAT_PROPERTY_TYPE:
        {
            distSearchInfo.sortPropertyFloatDataList_.push_back(std::make_pair(sortPropertyName, std::vector<float>()));
            std::vector<float>& dataList = distSearchInfo.sortPropertyFloatDataList_.back().second;
            dataList.resize(docNum);
            for (size_t i = 0; i < docNum; i++)
            {
                numericPropertyTable->getFloatValue(docIdList[i], dataList[i], false);
            }
        }
        break;
        case DOUBLE_PROPERTY_TYPE:
        {
            distSearchInfo.sortPropertyFloatDataList_.push_back(std::make_pair(sortPropertyName, std::vector<float>()));
            std::vector<float>& dataList = distSearchInfo.sortPropertyFloatDataList_.back().second;
            dataList.resize(docNum);
            for (size_t i = 0; i < docNum; i++)
            {
                numericPropertyTable->getFloatValue(docIdList[i], dataList[i], false);
            }
        }
        break;
        default:
            break;
        }
    }
}

void SearchManagerPreProcessor::preparePropertyTermIndex(
    const std::map<std::string, PropertyTermInfo>& propertyTermInfoMap,
    const std::vector<std::string>& indexPropertyList,
    std::vector<std::map<termid_t, unsigned> >& termIndexMaps)
{
    // use empty object for not found property
    const PropertyTermInfo emptyPropertyTermInfo;
    // build term index maps
    typedef std::vector<std::string>::const_iterator property_list_iterator;
    for (uint32_t i = 0; i < indexPropertyList.size(); ++i)
    {
        const PropertyTermInfo::id_uint_list_map_t& termPositionsMap =
            izenelib::util::getOr(
                propertyTermInfoMap,
                indexPropertyList[i],
                emptyPropertyTermInfo
            ).getTermIdPositionMap();

        unsigned index = 0;
        typedef PropertyTermInfo::id_uint_list_map_t::const_iterator
        term_id_position_iterator;
        for (term_id_position_iterator termIt = termPositionsMap.begin();
                termIt != termPositionsMap.end(); ++termIt)
        {
            termIndexMaps[i][termIt->first] = index++;
        }
    }
}

ProductScorer* SearchManagerPreProcessor::createProductScorer(
    const KeywordSearchActionItem& actionItem,
    PropSharedLockSet& propSharedLockSet,
    ProductScorer* relevanceScorer)
{
    std::auto_ptr<ProductScorer> relevanceScorerPtr(relevanceScorer);

    SearchingMode::SearchingModeType searchMode =
        actionItem.searchingMode_.mode_;

    if (searchMode != SearchingMode::SUFFIX_MATCH &&
        !hasSortByRankProp(actionItem.sortPriorityList_))
        return NULL;

    if (!isProductRanking_(actionItem))
        return relevanceScorerPtr.release();

    ProductScoreParam scoreParam(actionItem.env_.normalizedQueryString_,
                                 actionItem.env_.queryString_,
                                 actionItem.env_.querySource_,
                                 actionItem.groupParam_,
                                 propSharedLockSet,
                                 relevanceScorerPtr.release(),
                                 actionItem.searchingMode_.mode_,
                                 actionItem.queryScore_);
    return productScorerFactory_->createScorer(scoreParam);
}

bool SearchManagerPreProcessor::isProductRanking_(
    const KeywordSearchActionItem& actionItem) const
{
    return  (productScorerFactory_ != NULL);
}

bool SearchManagerPreProcessor::isNeedCustomDocIterator(
    const KeywordSearchActionItem& actionItem) const
{
    return hasSortByRankProp(actionItem.sortPriorityList_) &&
        isProductRanking_(actionItem);
}

bool SearchManagerPreProcessor::isNeedRerank(
    const KeywordSearchActionItem& actionItem) const
{
    return isSortByRankProp(actionItem.sortPriorityList_) &&
        isProductRanking_(actionItem);
}

bool SearchManagerPreProcessor::hasSortByRankProp(
    const KeywordSearchActionItem::SortPriorityList& sortPriorityList)
{
    for (KeywordSearchActionItem::SortPriorityList::const_iterator it =
             sortPriorityList.begin(); it != sortPriorityList.end(); ++it)
    {
        std::string propName = it->first;
        boost::to_lower(propName);
        if (propName == RANK_PROPERTY)
            return true;
    }

    return false;
}

bool SearchManagerPreProcessor::isSortByRankProp(
    const KeywordSearchActionItem::SortPriorityList& sortPriorityList)
{
    return sortPriorityList.size() == 1 &&
        hasSortByRankProp(sortPriorityList);
}
