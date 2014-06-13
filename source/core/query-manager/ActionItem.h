///
/// @file ActionItem.h
/// @brief header file of ActionItem classes
/// @author Dohyun Yun
/// @date 2008-06-05
/// @details
/// - Log
///     - 2009.06.11 Add getTaxonomyItem() & setTaxonomyItem() in commonInformation class by Dohyun Yun
///     - 2009.06.18 Add getTaxonomyIdList() & setTaxonomyIdList() in search keyword action item class by Dohyun Yun.
///                  From now on, SearchKeywordActionItem will be used for keyword search and taxonomy process.
///     - 2009.08.10 Change all the actionitems and remove the hierarchical structures. by Dohyun Yun
///     - 2009.08.21 Remove MF related codes by Dohyun Yun.
///     - 2009.09.01 Add operator =  in KeywordSearchActionItem to get the values from LabelClickActionItem
///     - 2009.09.08 Add serialize() function to KeywordSearchActionItem for using it to the MLRUCacue.
///     - 2009.10.10 Add search priority in KeywordSearchActionItem.
///     - 2011.04.19 Add custom ranking information in KeywordSearchActionItem by Zhongxia Li
///

#ifndef _ACTIONITEM_H_
#define _ACTIONITEM_H_

#include <common/type_defs.h>
#include <common/sf1_msgpack_serialization_types.h>
#include <common/parsers/ConditionsTree.h>
#include <3rdparty/msgpack/msgpack.hpp>

#include "QueryTypeDef.h"
#include "ConditionInfo.h"

#include <ranking-manager/RankingEnumerator.h>
#include <search-manager/CustomRanker.h>
#include <search-manager/GeoLocationRanker.h>
#include <mining-manager/group-manager/GroupParam.h>

#include <util/izene_serialization.h>
#include <net/aggregator/Util.h>

#include <sstream>
#include <vector>
#include <utility>

#include <boost/shared_ptr.hpp>

namespace sf1r {

///
/// @brief This class contains several information which is needed to display property.
///        One DisplayProperty contains all options of one specific property.
///
class DisplayProperty
{
public:
    DisplayProperty() :
        isSnippetOn_(false),
        isSummaryOn_(false),
        summarySentenceNum_(0),
        isHighlightOn_(false),
        isSplitPropertyValue_(false),
        isSubDocPropertyValue_(false)
    {}

    DisplayProperty(const std::string& obj) :
        isSnippetOn_(false),
        isSummaryOn_(false),
        summarySentenceNum_(0),
        summaryPropertyAlias_(),
        isHighlightOn_(false),
        isSplitPropertyValue_(false),
        isSubDocPropertyValue_(false),
        propertyString_(obj)
    {}

    void print(std::ostream& out = std::cout) const
    {
        stringstream ss;
        ss << endl;
        ss << "Class DisplayProperty" << endl;
        ss << "---------------------------------" << endl;
        ss << "Property : " << propertyString_ << endl;
        ss << "isSplitPropertyValue_ : " << isSplitPropertyValue_ << endl;
        ss << "isSubDocPropertyValue_ : " << isSubDocPropertyValue_ << endl;
        ss << "isHighlightOn_ : " << isHighlightOn_ << endl;
        ss << "isSnippetOn_   : " << isSnippetOn_   << endl;
        ss << "isSummaryOn_   : " << isSummaryOn_   << endl;
        ss << "Summary Sentence Number : " << summarySentenceNum_ << endl;
        ss << "Summary Property Alias : " << summaryPropertyAlias_ << endl;

        out << ss.str();
    }

    ///
    /// @brief a flag variable if snippet option is turned on in the query.
    ///
    bool isSnippetOn_;

    ///
    /// @brief a flag variable if summary option is turned on in the query.
    ///
    bool isSummaryOn_;

    ///
    /// @brief the number of summary sentences. It only affects if isSummaryOn_ is true.
    ///
    size_t summarySentenceNum_;

    /// @brief Summary is returned as another property. This field
    /// customized the name of that property. If it is empty, the name is
    /// determined by application.
    std::string summaryPropertyAlias_;

    ///
    /// @brief a flag variable if highlight option is turned on in the query.
    ///
    bool isHighlightOn_;

    ///
    /// @brief a flag variable, true for splitting property value into multiple values,
    /// mainly for properties configured in <Group> or <Attr>.
    ///
    bool isSplitPropertyValue_;

    ///
    /// @brief a flag for subdoc property
    ///
    bool isSubDocPropertyValue_;

    ///
    /// @brief analyzed query string of specific property
    ///
    std::string propertyString_;

    DATA_IO_LOAD_SAVE(DisplayProperty,
        & isSnippetOn_ & isSummaryOn_ & summarySentenceNum_
        & summaryPropertyAlias_ & isHighlightOn_ & isSplitPropertyValue_
        & isSubDocPropertyValue_ & propertyString_);

    MSGPACK_DEFINE(isSnippetOn_, isSummaryOn_, summarySentenceNum_,
        summaryPropertyAlias_, isHighlightOn_, isSplitPropertyValue_,
        isSubDocPropertyValue_, propertyString_);

private:
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & isSnippetOn_;
        ar & isSummaryOn_;
        ar & summarySentenceNum_;
        ar & summaryPropertyAlias_;
        ar & isHighlightOn_;
        ar & isSplitPropertyValue_;
        ar & isSubDocPropertyValue_;
        ar & propertyString_;
    }
}; // end - class DisplayProperty

inline bool operator==(const DisplayProperty& a, const DisplayProperty& b)
{
    return a.isSnippetOn_ == b.isSnippetOn_
        && a.isSummaryOn_ == b.isSummaryOn_
        && a.summarySentenceNum_ == b.summarySentenceNum_
        && a.isHighlightOn_ == b.isHighlightOn_
        && a.isSplitPropertyValue_ == b.isSplitPropertyValue_
        && a.isSubDocPropertyValue_ == b.isSubDocPropertyValue_
        && a.propertyString_ == b.propertyString_
        && a.summaryPropertyAlias_ == b.summaryPropertyAlias_;
} // end - operator==()

///
/// @brief This class contains environment values of query requester.
///        It is stored in most of the ActionItems.
///
class RequesterEnvironment
{
public:

    RequesterEnvironment() : isLogging_(false) {}

    void print(std::ostream& out = std::cout) const
    {
        stringstream ss;
        ss << endl;
        ss << "RequesterEnvironment"                    << endl;
        ss << "------------------------------"          << endl;
        ss << "isLogging        : " << isLogging_       << endl;
        ss << "encodingType_    : " << encodingType_    << endl;
        ss << "queryString_     : " << queryString_     << endl;
        ss << "ipAddress_       : " << ipAddress_       << endl;
        ss << "querySource      : " << querySource_     << endl;
        out << ss.str();
    }

    ///
    /// @brief a flag if current query should be logged.
    ///
    bool isLogging_;

    ///
    /// @brief encoding type string.
    ///
    std::string encodingType_;

    ///
    /// @brief a query string. The encoding type of query string
    ///        and result XML will follow encodingType_.
    ///
    std::string queryString_;

    ///
    /// @brief the expanded query string.
    ///
    std::string expandedQueryString_;

    ///
    /// @brief the normalized query string.
    ///
    std::string normalizedQueryString_;

    ///
    /// @brief a user id. The user id is accompanied with the query string
    ///          in a search request.
    ///
    std::string userID_;

    ///
    /// @brief ip address of requester.
    ///
    std::string ipAddress_;

    ///
    /// @brief where does the query come from, used to decide
    ///        the categories to boost in product ranking.
    ///
    std::string querySource_;

    DATA_IO_LOAD_SAVE(RequesterEnvironment,
            & isLogging_ & encodingType_
            & queryString_ & expandedQueryString_ & normalizedQueryString_
            & userID_ & ipAddress_ & querySource_);

    MSGPACK_DEFINE(isLogging_, encodingType_,
            queryString_, expandedQueryString_, normalizedQueryString_,
            userID_, ipAddress_, querySource_);

private:
    // Log : 2009.09.08
    // ---------------------------------------
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & isLogging_;
        ar & encodingType_;
        ar & queryString_;
        ar & expandedQueryString_;
        ar & normalizedQueryString_;
        ar & userID_;
        ar & ipAddress_;
        ar & querySource_;
    }
}; // end - queryEnvironment

inline bool operator==(
        const RequesterEnvironment& a,
        const RequesterEnvironment& b
)
{
    return a.isLogging_ == b.isLogging_
        && a.encodingType_ == b.encodingType_
        && a.queryString_ == b.queryString_
        && a.ipAddress_ == b.ipAddress_
        && a.querySource_ == b.querySource_;
}

///
/// @brief This class has information to process keyword searching.
///
class KeywordSearchActionItem
{
public:
    // property name - sorting order(true : Ascending, false : Descending)
    typedef std::pair<std::string , bool> SortPriorityType;
    typedef std::vector<SortPriorityType> SortPriorityList;
    // Filter Option - propertyName

    KeywordSearchActionItem()
        : disableGetDocs_(false)
        , removeDuplicatedDocs_(false)
        , isRandomRank_(false)
        , requireRelatedQueries_(false)
        , isAnalyzeResult_(false)
    {
    }

    KeywordSearchActionItem(const KeywordSearchActionItem& obj)
        : env_(obj.env_)
        , refinedQueryString_(obj.refinedQueryString_)
        , queryScore_(0)
        , collectionName_(obj.collectionName_)
        , rankingType_(obj.rankingType_)
        , searchingMode_(obj.searchingMode_)
        , pageInfo_(obj.pageInfo_)
        , disableGetDocs_(obj.disableGetDocs_)
        , languageAnalyzerInfo_(obj.languageAnalyzerInfo_)
        , searchPropertyList_(obj.searchPropertyList_)
        , removeDuplicatedDocs_(obj.removeDuplicatedDocs_)
        , displayPropertyList_(obj.displayPropertyList_)
        , sortPriorityList_(obj.sortPriorityList_)
        , filterTree_(obj.filterTree_)
        , rangePropertyName_(obj.rangePropertyName_)
        , groupParam_(obj.groupParam_)
        , strExp_(obj.strExp_)
        , paramConstValueMap_(obj.paramConstValueMap_)
        , paramPropertyValueMap_(obj.paramPropertyValueMap_)
        , customRanker_(obj.customRanker_)
        , geoLocationRanker_(obj.geoLocationRanker_)
        , isRandomRank_(obj.isRandomRank_)
        , requireRelatedQueries_(obj.requireRelatedQueries_)
        , isAnalyzeResult_(obj.isAnalyzeResult_)
    {
    }


    KeywordSearchActionItem& operator=(const KeywordSearchActionItem& obj)
    {
        env_ = obj.env_;
        refinedQueryString_ = obj.refinedQueryString_;
        collectionName_ = obj.collectionName_;
        rankingType_ = obj.rankingType_;
        searchingMode_ = obj.searchingMode_;
        pageInfo_ = obj.pageInfo_;
        disableGetDocs_ = obj.disableGetDocs_;
        languageAnalyzerInfo_ = obj.languageAnalyzerInfo_;
        searchPropertyList_ = obj.searchPropertyList_;
        removeDuplicatedDocs_ = obj.removeDuplicatedDocs_;
        displayPropertyList_ = obj.displayPropertyList_;
        sortPriorityList_    = obj.sortPriorityList_;
        filterTree_ = obj.filterTree_;
        rangePropertyName_ = obj.rangePropertyName_;
        groupParam_ = obj.groupParam_;
        strExp_ = obj.strExp_;
        paramConstValueMap_ = obj.paramConstValueMap_;
        paramPropertyValueMap_ = obj.paramPropertyValueMap_;
        customRanker_ = obj.customRanker_;
        geoLocationRanker_ = obj.geoLocationRanker_;
        isRandomRank_ = obj.isRandomRank_;
        requireRelatedQueries_ = obj.requireRelatedQueries_;
        isAnalyzeResult_ = obj.isAnalyzeResult_;

        return (*this);
    }

    bool operator==(const KeywordSearchActionItem& obj) const
    {
        return env_ == obj.env_
            && collectionName_ == obj.collectionName_
            && refinedQueryString_ == obj.refinedQueryString_
            && rankingType_ == obj.rankingType_
            && searchingMode_ == obj.searchingMode_
            && pageInfo_ == obj.pageInfo_
            && disableGetDocs_ == obj.disableGetDocs_
            && languageAnalyzerInfo_ == obj.languageAnalyzerInfo_
            && searchPropertyList_ == obj.searchPropertyList_
            && removeDuplicatedDocs_ == obj.removeDuplicatedDocs_
            && displayPropertyList_ == obj.displayPropertyList_
            && sortPriorityList_    == obj.sortPriorityList_
            && filterTree_ == obj.filterTree_
            && rangePropertyName_ == obj.rangePropertyName_
            && groupParam_ == obj.groupParam_
            && strExp_ == obj.strExp_
            && paramConstValueMap_ == obj.paramConstValueMap_
            && paramPropertyValueMap_ == obj.paramPropertyValueMap_
            && customRanker_ == obj.customRanker_
            && geoLocationRanker_ == obj.geoLocationRanker_
            && isRandomRank_ == obj.isRandomRank_
            && requireRelatedQueries_ == obj.requireRelatedQueries_
            && isAnalyzeResult_ == obj.isAnalyzeResult_;
    }

    void print(std::ostream& out = std::cout) const
    {
        stringstream ss;
        ss << endl;
        ss << "Class KeywordSearchActionItem" << endl;
        ss << "-----------------------------------" << endl;
        ss << "Collection Name  : " << collectionName_ << endl;
        ss << "Refined Query    : " << refinedQueryString_ << endl;
        ss << "RankingType      : " << rankingType_ << endl;
        ss << "SearchingMode    : " << searchingMode_.mode_ << " , "
                                    << searchingMode_.threshold_ <<", "
                                    << searchingMode_.useOriginalQuery_<<endl;

        ss << "PageInfo         : " << pageInfo_.start_ << " , " << pageInfo_.count_ << endl;
        ss << "disableGetDocs_  : " << disableGetDocs_ << endl;
        ss << "LanguageAnalyzer : " << languageAnalyzerInfo_.applyLA_ << " , "
                                    << languageAnalyzerInfo_.useOriginalKeyword_ << " , "
                                    << languageAnalyzerInfo_.synonymExtension_ << endl;
        ss << "Search Property  : ";
        for (size_t i = 0; i < searchPropertyList_.size(); i++)
            ss << searchPropertyList_[i] << " ";
        ss << endl;
        ss << "removeDuplicate  : " << removeDuplicatedDocs_ << endl;
        env_.print(out);
        for (size_t i = 0; i < displayPropertyList_.size(); i++)
            displayPropertyList_[i].print(out);

        ss << "Sort Priority : " << endl;
        for (size_t i = 0; i < sortPriorityList_.size(); i++)
            ss << " - " << sortPriorityList_[i].first << " " << sortPriorityList_[i].second << endl;

        ss << "Filtering Option : " << endl;
        /*for (size_t i = 0; i < filteringList_.size(); i++)
        {
            ss << "FilteringType :  " << filteringList_[i].operation_ << " , property : " << filteringList_[i].property_ << endl;
            ss << "------------------------------------------------" << endl;
            for( std::vector<PropertyValue>::const_iterator iter = filteringList_[i].values_.begin();
                    iter != filteringList_[i].values_.end(); iter++ )
                ss << *iter << ", type:" << iter->which() << endl;
            ss << "------------------------------------------------" << endl;
        }*/
        ss << "------------------------------------------------" << endl;
        ss << groupParam_;
        ss << "------------------------------------------------" << endl;
        ss << endl << "Custom Ranking :" << endl;
        ss << "------------------------------------------------" << endl;
        ss << "\tExpression : " << strExp_ << endl;
        std::map<std::string, double>::const_iterator diter;
        for(diter = paramConstValueMap_.begin(); diter != paramConstValueMap_.end(); diter++)
        {
            ss << "\tParameter : " << diter->first << ", Value : " << diter->second << endl;
        }
        std::map<std::string, std::string>::const_iterator siter;
        for(siter = paramPropertyValueMap_.begin(); siter != paramPropertyValueMap_.end(); siter++)
        {
            ss << "\tParameter : " << siter->first << ", Value : " << siter->second << endl;
        }
        ss << "------------------------------------------------" << endl;

        ss << "isRandomRank_: " << isRandomRank_ << endl;
        ss << "------------------------------------------------" << endl;

        ss << "requireRelatedQueries_: " << requireRelatedQueries_ << endl;
        ss << "------------------------------------------------" << endl;

        ss << "isAnalyzeResult_: " << isAnalyzeResult_ << endl;
        ss << "------------------------------------------------" << endl;

        out << ss.str();
    }

    ///
    /// @brief an environment variable of requester.
    /// @see RequesterEnvironment
    ///
    RequesterEnvironment env_;

    ///
    /// @brief a string of corrected query. It will be filled by query-correction-sub-manager
    ///        if it needs to be modified.
    ///
    izenelib::util::UString refinedQueryString_;

    ///
    /// @brief the score of the query in product tokenizer; only used in TitleScore Relevance;
    ///
    double queryScore_;

    ///
    /// @brief a collection name.
    ///
    std::string collectionName_;

    ///
    /// @brief ranking type of the query. BM25, KL and PLM can be used.
    ///
    RankingType::TextRankingType rankingType_;

    ///
    /// @brief searching mode of the query. AND, OR, VERBOSE, KNN and SUFFIX_MATCH can be used.
    ///
    SearchingModeInfo searchingMode_;

    ///
    /// @brief page information of current result page.
    /// @see PageInfo
    ///
    PageInfo pageInfo_;
    bool  disableGetDocs_;

    ///
    /// @brief This contains how to analyze input query string.
    ///
    LanguageAnalyzerInfo languageAnalyzerInfo_;

    ///
    /// @brief a list of properties which are the target of searching.
    ///
    std::vector<std::string> searchPropertyList_;

    ///
    /// @brief a flag if duplicated documents are removed in the result.
    ///
    bool removeDuplicatedDocs_;

    ///
    /// @brief a list of property which are the target of display in the result.
    ///
    std::vector<DisplayProperty> displayPropertyList_;

    ///
    /// @brief a list of sort methods. The less the index of a sort item in  the list, the more it has priority.
    ///
    SortPriorityList sortPriorityList_;

    ///
    /// @brief a list of filtering option.
    ///
    //std::vector<QueryFiltering::FilteringType> filteringList_;

    ///
    /// @brief a list of new tree-like filteringType for bool combination option
    ///
    ///std::vector<QueryFiltering::FilteringTreeValue> filteringTreeList_;
    ConditionsNode filterTree_;
    ///
    /// @brief a list of counters
    ///
    std::vector<std::string> counterList_;

    ///
    /// @brief property name for getting its property value range.
    ///
    std::string rangePropertyName_;

    ///
    /// @brief group filter parameter
    ///
    faceted::GroupParam groupParam_;

    ///
    /// @brief custom ranking information
    ///
    std::string strExp_;
    std::map<std::string, double> paramConstValueMap_;
    std::map<std::string, std::string> paramPropertyValueMap_;

    ///
    /// @brief Information that are used to retrieve ads
    ///
    std::vector<std::pair<std::string, std::string> > adSearchPropertyValue_;

    ///
    /// @brief custom ranking information(2)
    /// Avoid a second parsing by passing a reference to CustomRanker object.
    /// TODO, abandon this, serialization needed for remoted call
    CustomRankerPtr customRanker_;

    std::pair<double, double> geoLocation_;
    std::string geoLocationProperty_;
    GeoLocationRankerPtr geoLocationRanker_;

    bool isRandomRank_;

    ///
    /// @brief whether contain related queries in the response
    bool requireRelatedQueries_;

    ///
    /// @brief If true, return "analyzer_result" in response,
    /// which contains the tokenized result of the query
    ///
    bool isAnalyzeResult_;

    DATA_IO_LOAD_SAVE(KeywordSearchActionItem, & env_ & refinedQueryString_ & collectionName_
             & rankingType_ & searchingMode_ & pageInfo_ & disableGetDocs_ & languageAnalyzerInfo_ & searchPropertyList_ & removeDuplicatedDocs_
             & displayPropertyList_ & sortPriorityList_ & filterTree_ & counterList_ & rangePropertyName_ & groupParam_
             & strExp_ & paramConstValueMap_ & paramPropertyValueMap_ & isRandomRank_ & requireRelatedQueries_ & isAnalyzeResult_);

    /// msgpack serializtion
    MSGPACK_DEFINE(env_, refinedQueryString_, collectionName_, rankingType_, searchingMode_, pageInfo_, disableGetDocs_, languageAnalyzerInfo_,
            searchPropertyList_, removeDuplicatedDocs_, displayPropertyList_, sortPriorityList_, filterTree_, counterList_,
            rangePropertyName_, groupParam_, strExp_, paramConstValueMap_, paramPropertyValueMap_, isRandomRank_, requireRelatedQueries_,
            isAnalyzeResult_);

private:
    // Log : 2009.09.08
    // ---------------------------------------
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & env_;
        ar & refinedQueryString_;
        ar & collectionName_;
        ar & rankingType_;
        ar & searchingMode_;
        ar & pageInfo_;
        ar & disableGetDocs_;
        ar & languageAnalyzerInfo_;
        ar & searchPropertyList_;
        ar & removeDuplicatedDocs_;
        ar & displayPropertyList_;
        ar & sortPriorityList_;
        ar & counterList_;
        ar & rangePropertyName_;
        ar & groupParam_;
        ar & strExp_;
        ar & paramConstValueMap_;
        ar & paramPropertyValueMap_;
        ar & isRandomRank_;
        ar & requireRelatedQueries_;
        ar & isAnalyzeResult_;
    }

}; // end - class KeywordSearchActionItem

/// @brief Information to process documents get request.
///
/// Get document by internal IDs or user specified DOCID's.
class GetDocumentsByIdsActionItem
{
public:
    /// @brief an environment value of requester.
    RequesterEnvironment env_;

    /// @brief This contains how to analyze input query string.
    LanguageAnalyzerInfo languageAnalyzerInfo_;

    /// @brief Collection name.
    std::string collectionName_;

    /// @brief List of display properties which is shown in the result.
    std::vector<DisplayProperty> displayPropertyList_;

    /// @brief Internal document id list.
    std::vector<wdocid_t> idList_;

    /// @brief User specified DOCID list.
    std::vector<std::string> docIdList_;

    /// @brief Property name, and values list
    std::string propertyName_;
    std::vector<PropertyValue> propertyValueList_;

    /// @brief filtering options
    //std::vector<QueryFiltering::FilteringType> filteringList_;

	/// @brief filtering tree-likes options
    ConditionsNode filterTree_;

    void print(std::ostream& out = std::cout) const
    {
        stringstream ss;
        ss << endl;
        env_.print(out);
        ss << "collection: " << collectionName_ << std::endl;
        ss << "DisplayProperty: ";
        for (size_t i = 0; i < displayPropertyList_.size(); ++i)
        {
            displayPropertyList_[i].print(out);
        }
        ss << "Internal docid: ";
        for (size_t i = 0; i < idList_.size(); ++i)
        {
            ss << idList_[i] << ", ";
        }
        ss << std::endl;
        ss << "DOCID list: ";
        for (size_t i = 0; i < docIdList_.size(); ++i)
        {
            ss << docIdList_[i] << ",";
        }
        ss << std::endl;
        ss << "PropertyName : " << propertyName_ << std::endl;
        ss << "Filtering Option : " << endl;
        /*for (size_t i = 0; i < filteringList_.size(); i++)
        {
            ss << "FilteringType :  " << filteringList_[i].operation_ << " , property : " << filteringList_[i].property_ << endl;
            ss << "------------------------------------------------" << endl;
            for( std::vector<PropertyValue>::const_iterator iter = filteringList_[i].values_.begin();
                    iter != filteringList_[i].values_.end(); iter++ )
                ss << *iter << ", type:" << iter->which() << endl;
            ss << "------------------------------------------------" << endl;
        }*/
        out << ss.str();
    }

    DATA_IO_LOAD_SAVE(
        GetDocumentsByIdsActionItem,
        & env_ & languageAnalyzerInfo_ & collectionName_ & displayPropertyList_
        & idList_ & docIdList_ & propertyName_ & propertyValueList_
    )

    MSGPACK_DEFINE(env_, languageAnalyzerInfo_, collectionName_, displayPropertyList_,
            idList_, docIdList_, propertyName_, propertyValueList_);

public:
    std::set<sf1r::workerid_t> getDocWorkerIdLists(
            std::vector<sf1r::docid_t>& docidList,
            std::vector<sf1r::workerid_t>& workeridList) const
    {
        std::set<sf1r::workerid_t> workerSet;
        for (size_t i = 0; i < idList_.size(); i++)
        {
            std::pair<sf1r::workerid_t, docid_t> wd = net::aggregator::Util::GetWorkerAndDocId(idList_[i]);
            workeridList.push_back(wd.first);
            docidList.push_back(wd.second);

            workerSet.insert(wd.first);
        }

        return workerSet;
    }

private:
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar & env_ & languageAnalyzerInfo_ & collectionName_ & displayPropertyList_
            & idList_ & docIdList_ & filterTree_;
    }
}; // end - class GetDocumentsByIdsActionItem

///
/// @brief Information for click group label.
///
class ClickGroupLabelActionItem
{
public:
    ClickGroupLabelActionItem() {}

    ClickGroupLabelActionItem(
            const std::string& queryString,
            const std::string& propName,
            const std::vector<std::string>& groupPath)
        : queryString_(queryString)
        , propName_(propName)
        , groupPath_(groupPath)
    {}

    std::string queryString_;

    std::string propName_;

    std::vector<std::string> groupPath_;

    MSGPACK_DEFINE(queryString_, propName_, groupPath_);
};

} // end - namespace sf1r

#endif // _ACTION_ITEM_
