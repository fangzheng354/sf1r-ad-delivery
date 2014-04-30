///
/// @file   ResultType.h
/// @brief  Group of Result data types.
/// @author Dohyun Yun
/// @date   2009.08.12
/// @details
/// - Log
///     - 2009.08.27 Added DocumentClickResultFromMIA by Dohyun Yun
///     - 2009.08.31 Added RawTextResultFromSIA by Dohyun Yun
///     - 2009.12.02 Added ErrorInfo class by Dohyun Yun
///     - 2011.04.22 Added topKCustomRankScoreList_ in KeywordSearchResult by Zhongxia Li
///

#ifndef _RESULTTYPE_H_
#define _RESULTTYPE_H_

#include <common/type_defs.h>
#include <common/PropertyValue.h>
#include <common/sf1_msgpack_serialization_types.h>
#include <query-manager/ConditionInfo.h>
#include <mining-manager/group-manager/ontology_rep.h>
#include <mining-manager/group-manager/GroupRep.h>
#include <mining-manager/group-manager/GroupParam.h>
#include <idmlib/concept-clustering/cc_types.h>

#include <util/ustring/UString.h>
#include <util/izene_serialization.h>

#include <3rdparty/msgpack/msgpack.hpp>
#include <net/aggregator/Util.h>

#include <sstream>
#include <vector>
#include <iterator>
#include <algorithm>

namespace sf1r
{

struct PropertyRange
{
    PropertyRange()
        : highValue_(0)
        , lowValue_(0)
    {}
    void swap(PropertyRange& range)
    {
        using std::swap;
        swap(highValue_, range.highValue_);
        swap(lowValue_, range.lowValue_);
    }
    float highValue_;
    float lowValue_;

    MSGPACK_DEFINE(highValue_, lowValue_);
};


///
/// @brief This class is inheritable type of every result type in this file.
///
class ErrorInfo
{
public:
    ErrorInfo() : errno_(0) {};
    ~ErrorInfo() {};

    ///
    /// @brief system defined error no.
    ///
    int         errno_;

    ///
    /// @brief detail error message of the error.
    ///
    std::string error_;

    DATA_IO_LOAD_SAVE(ErrorInfo, &errno_&error_);

    MSGPACK_DEFINE(errno_,error_);
}; // end - class ErrorInfo

typedef ErrorInfo ResultBase;

// Definition of divided search results
#include "DistributeResultType.h"


class KeywordSearchResult : public ErrorInfo
{
public:

    KeywordSearchResult()
        : encodingType_(izenelib::util::UString::UTF_8)
        , totalCount_(0), docsInPage_(0), topKDocs_(0), adCachedTopKDocs_(0)
        , topKRankScoreList_(0), topKCustomRankScoreList_(0)
        , start_(0), count_(0)
        , timeStamp_(0), TOP_K_NUM(0)
    {
    }

    void print(std::ostream& out = std::cout) const
    {
        stringstream ss;
        ss << endl;
        ss << "==== Class KeywordSearchResult ====" << endl;
        ss << "-----------------------------------" << endl;
        ss << "rawQueryString_    : " << rawQueryString_ << endl;
        ss << "pruneQueryString_  : " << pruneQueryString_ << endl;
        ss << "encodingType_      : " << encodingType_ << endl;
        ss << "collectionName_    : " << collectionName_ << endl;
        ss << "analyzedQuery_     : " ;
        string s;
        analyzedQuery_.convertString(s, izenelib::util::UString::UTF_8);
        ss << s << ", ";
        ss << endl;
        ss << "queryTermIdList_   : " ;
        for (size_t i = 0; i < queryTermIdList_.size(); i ++)
        {
            ss << queryTermIdList_[i] << ", ";
        }
        ss << endl;
        ss << "totalCount_        : " << totalCount_ << endl;
        ss << "docsInPage         : " << docsInPage_.size() << endl;
        ss << "topKDocs_          : " << topKDocs_.size() << endl;
        for (size_t i = 0; i < topKDocs_.size(); i ++)
        {
            ss << "0x"<< hex << topKDocs_[i] << ", ";
        }
        ss << dec<< endl;
        ss << "topKWorkerIds_      : " << topKWorkerIds_.size() << endl;
        for (size_t i = 0; i < topKWorkerIds_.size(); i ++)
        {
            ss << topKWorkerIds_[i] << ", ";
        }
        ss << endl;
        std::vector<sf1r::wdocid_t> topKWDocs;
        const_cast<KeywordSearchResult*>(this)->getTopKWDocs(topKWDocs);
        ss << "topKWDocs          : " << topKWDocs.size() << endl;
        for (size_t i = 0; i < topKWDocs.size(); i ++)
        {
            ss << "0x" << hex<< topKWDocs[i] << ", ";
        }
        ss << dec<< endl;
        ss << "topKRankScoreList_      : " << topKRankScoreList_.size() << endl;
        for (size_t i = 0; i < topKRankScoreList_.size(); i ++)
        {
            ss << topKRankScoreList_[i] << ", ";
        }
        ss << endl;
        ss << "topKCustomRankScoreList_: " << endl;
        for (size_t i = 0; i < topKCustomRankScoreList_.size(); i ++)
        {
            ss << topKCustomRankScoreList_[i] << ", ";
        }
        ss << endl;
        ss << "page start_    : " << start_ << " count_   : " << count_ << endl;
        ss << endl;
        ss << "pageOffsetList_          : " << pageOffsetList_.size() << endl;
        for (size_t i = 0; i < pageOffsetList_.size(); i ++)
        {
            ss << pageOffsetList_[i] << ", ";
        }
        ss << endl << endl;

        ss << "propertyQueryTermList_ : " << endl;
        for (size_t i = 0; i < propertyQueryTermList_.size(); i++)
        {
            for (size_t j = 0; j < propertyQueryTermList_[i].size(); j++)
            {
                string s;
                propertyQueryTermList_[i][j].convertString(s, izenelib::util::UString::UTF_8);
                ss << s << ", ";
            }
            ss << endl;
        }
        ss << endl;

        ss << "fullTextOfDocumentInPage_      : " << fullTextOfDocumentInPage_.size() << endl;
        for (size_t i = 0; i < fullTextOfDocumentInPage_.size(); i++)
        {
            for (size_t j = 0; j < fullTextOfDocumentInPage_[i].size(); j++)
            {
            }
            ss << fullTextOfDocumentInPage_[i].size() << endl;
        }
        ss << endl;
        ss << "snippetTextOfDocumentInPage_   : " << snippetTextOfDocumentInPage_.size() << endl;
        for (size_t i = 0; i < snippetTextOfDocumentInPage_.size(); i++)
        {
            for (size_t j = 0; j < snippetTextOfDocumentInPage_[i].size(); j++)
            {
                //string s;
                //snippetTextOfDocumentInPage_[i][j].convertString(s, izenelib::util::UString::UTF_8);
                //ss << s << ", ";
            }
            ss << snippetTextOfDocumentInPage_[i].size() << endl;
        }
        ss << endl;
        ss << "rawTextOfSummaryInPage_        : " << rawTextOfSummaryInPage_.size() << endl;
        for (size_t i = 0; i < rawTextOfSummaryInPage_.size(); i++)
        {
            for (size_t j = 0; j < rawTextOfSummaryInPage_[i].size(); j++)
            {
            }
            ss << rawTextOfSummaryInPage_[i].size() << endl;
        }
        ss << endl;

        ss << "numberOfDuplicatedDocs_        : " << numberOfDuplicatedDocs_.size() << endl;
        for (size_t i = 0; i < numberOfDuplicatedDocs_.size(); i ++)
        {
            ss << numberOfDuplicatedDocs_[i] << ", ";
        }
        ss << endl;
        ss << "numberOfSimilarDocs_           : " << numberOfSimilarDocs_.size() << endl;
        for (size_t i = 0; i < numberOfSimilarDocs_.size(); i ++)
        {
            ss << numberOfSimilarDocs_[i] << ", ";
        }
        ss << endl;
        ss << "docCategories_          : " << endl;
        for (size_t i = 0; i < docCategories_.size(); i++)
        {
            for (size_t j = 0; j < docCategories_[i].size(); j++)
            {
                string s;
                s = propstr_to_str(docCategories_[i][j]);
                ss << s << ", ";
            }
            ss << endl;
        }

        ss << endl;
        ss << "groupRep_ : " <<endl;
        ss << groupRep_.ToString();
        ss << "attrRep_ : " <<endl;
        ss << attrRep_.ToString();

        using namespace faceted;
        ss << "autoSelectGroupLabels_:" << endl;
        ss << autoSelectGroupLabels_ << endl;

        ss << "TOP_K_NUM : " << TOP_K_NUM << endl;
        ss << "Finish time : " << std::ctime(&timeStamp_) << endl;

        ss << "===================================" << endl;
        out << ss.str();
    }

    std::string rawQueryString_;

    std::string pruneQueryString_;

    /// Distributed search info
    DistKeywordSearchInfo distSearchInfo_;

    ///
    /// @brief encoding type of rawQueryString
    ///
    izenelib::util::UString::EncodingType encodingType_;

    std::string collectionName_;
    /// Analyzed query string.
    izenelib::util::UString analyzedQuery_;

    std::vector<termid_t> queryTermIdList_;

    /// Total number of result documents
    std::size_t totalCount_;

    std::map<std::string,uint32_t> counterResults_;

    std::vector<docid_t> docsInPage_;
    /// A list of ranked docId. First docId gets high rank score.
    std::vector<docid_t> topKDocs_;
    std::vector<docid_t> adCachedTopKDocs_;

    /// A list of workerids. The sequence is following \c topKDocs_.
    std::vector<uint32_t> topKWorkerIds_;

    std::vector<uint32_t> topKtids_;

    /// A list of rank scores. The sequence is following \c topKDocs_.
    std::vector<float> topKRankScoreList_;

    /// A list of custom ranking scores. The sequence is following \c topKDocs_.
    std::vector<float> topKCustomRankScoreList_;

    PropertyRange propertyRange_;

    std::size_t start_;

    /// @brief number of documents in current page
    std::size_t count_;

    /// For results in page in one node, indicates corresponding postions in that page result.
    std::vector<size_t> pageOffsetList_;

    /// property query terms
    std::vector<std::vector<izenelib::util::UString> > propertyQueryTermList_;

    /// @brief Full text of documents in one page. It will be used for
    /// caching in BA when "DocumentClick" query occurs.
    /// @see pageInfo_
    ///
    /// @details
    /// ProtoType : fullTextOfDocumentInPage_[DISPLAY PROPERTY Order][DOC Order]
    /// - DISPLAY PROPERTY Order : The sequence which follows the order in displayPropertyList_
    /// - DOC Order : The sequence which follows the order in topKDocs_
    std::vector<std::vector<PropertyValue::PropertyValueStrType> >  fullTextOfDocumentInPage_;

    /// @brief Displayed text of documents in one page.
    /// @see pageInfo_
    ///
    /// The first index corresponds to each property in \c
    /// KeywordSearchActionItem::displayPropertylist with the same
    /// order.
    ///
    /// The inner vector is raw text for documents in current page.
    std::vector<std::vector<PropertyValue::PropertyValueStrType> >  snippetTextOfDocumentInPage_;

    /// @brief Summary of documents in one page.
    /// @see pageInfo_
    ///
    /// The first index corresponds to each property in \c
    /// KeywordSearchActionItem::displayPropertylist which is marked
    /// generating summary with the same order.
    /// The inner vector is summary text for documents in current page.
    //
    /// (ex) Display Property List : title, content, attach.
    ///      Summary Option On : title, attach.
    ///
    ///      size of rawTextOfSummaryInPage_ == 2
    ///      rawTextOfSummaryInPage_[0][1] -> summary of second document[1] of title property[0].
    ///      rawTextOfSummaryInPage_[1][3] -> summary of fourth document[3] of attach property[0].
    ///
    ///
    std::vector<std::vector<PropertyValue::PropertyValueStrType> >  rawTextOfSummaryInPage_;


    std::vector<count_t> numberOfDuplicatedDocs_;

    std::vector<count_t> numberOfSimilarDocs_;

    std::vector<std::vector<PropertyValue::PropertyValueStrType> > docCategories_;


    // a list, each element is a label tree for a property
    sf1r::faceted::GroupRep groupRep_;

    // a list, each element is a label array for an attribute name
    sf1r::faceted::OntologyRep attrRep_;

    // auto selected top group labels
    sf1r::faceted::GroupParam::GroupLabelScoreMap autoSelectGroupLabels_;

    /// A list of related query string.
    std::deque<izenelib::util::UString> relatedQueryList_;

    /// A list of related query rank score.
    std::vector<float> rqScore_;

    /// Finish time of searching
    std::time_t timeStamp_;

    // the max possible returned result.
    int TOP_K_NUM;

    void getTopKWDocs(std::vector<sf1r::wdocid_t>& topKWDocs) const
    {
        if (topKWorkerIds_.size() <= 0)
        {
            topKWDocs.assign(topKDocs_.begin(), topKDocs_.end());
            return;
        }

        topKWDocs.resize(topKDocs_.size());
        for (size_t i = 0; i < topKDocs_.size(); i++)
        {
            topKWDocs[i] = net::aggregator::Util::GetWDocId(topKWorkerIds_[i], topKDocs_[i]);
        }
    }

    void setStartCount(const PageInfo& pageInfo)
    {
        start_ = pageInfo.start_;
        count_ = pageInfo.count_;
    }

    void adjustStartCount(std::size_t topKStart)
    {
        std::size_t topKEnd = topKStart + topKDocs_.size();

        if (start_ > topKEnd)
        {
            start_ = topKEnd;
        }

        if (start_ + count_ > topKEnd)
        {
            count_ = topKEnd - start_;
        }
    }

    void swap(KeywordSearchResult& other)
    {
        using std::swap;
        rawQueryString_.swap(other.rawQueryString_);
        pruneQueryString_.swap(other.pruneQueryString_);
        distSearchInfo_.swap(other.distSearchInfo_);
        swap(encodingType_, other.encodingType_);
        collectionName_.swap(other.collectionName_);
        analyzedQuery_.swap(other.analyzedQuery_);
        queryTermIdList_.swap(other.queryTermIdList_);
        swap(totalCount_, other.totalCount_);
        counterResults_.swap(other.counterResults_);
        docsInPage_.swap(other.docsInPage_);
        topKDocs_.swap(other.topKDocs_);
        adCachedTopKDocs_.swap(other.adCachedTopKDocs_);
        topKWorkerIds_.swap(other.topKWorkerIds_);
        topKtids_.swap(other.topKtids_);
        topKRankScoreList_.swap(other.topKRankScoreList_);
        topKCustomRankScoreList_.swap(other.topKCustomRankScoreList_);
        propertyRange_.swap(other.propertyRange_);
        swap(start_, other.start_);
        swap(count_, other.count_);
        pageOffsetList_.swap(other.pageOffsetList_);
        propertyQueryTermList_.swap(other.propertyQueryTermList_);
        fullTextOfDocumentInPage_.swap(other.fullTextOfDocumentInPage_);
        snippetTextOfDocumentInPage_.swap(other.snippetTextOfDocumentInPage_);
        rawTextOfSummaryInPage_.swap(other.rawTextOfSummaryInPage_);
        numberOfDuplicatedDocs_.swap(other.numberOfDuplicatedDocs_);
        numberOfSimilarDocs_.swap(other.numberOfSimilarDocs_);
        docCategories_.swap(other.docCategories_);
        groupRep_.swap(other.groupRep_);
        attrRep_.swap(other.attrRep_);
        autoSelectGroupLabels_.swap(other.autoSelectGroupLabels_);
        relatedQueryList_.swap(other.relatedQueryList_);
        rqScore_.swap(other.rqScore_);
        swap(timeStamp_, other.timeStamp_);
        TOP_K_NUM = other.TOP_K_NUM;
    }

    MSGPACK_DEFINE(
            rawQueryString_, pruneQueryString_, distSearchInfo_, encodingType_, collectionName_, analyzedQuery_,
            queryTermIdList_, totalCount_, counterResults_, docsInPage_, topKDocs_, adCachedTopKDocs_, topKWorkerIds_, topKtids_, topKRankScoreList_,
            topKCustomRankScoreList_, propertyRange_, start_, count_, pageOffsetList_, propertyQueryTermList_, fullTextOfDocumentInPage_,
            snippetTextOfDocumentInPage_, rawTextOfSummaryInPage_,
            numberOfDuplicatedDocs_, numberOfSimilarDocs_, docCategories_,
            groupRep_, attrRep_, autoSelectGroupLabels_, relatedQueryList_, rqScore_, timeStamp_, TOP_K_NUM);
};


class RawTextResultFromSIA : public ErrorInfo // Log : 2009.08.31
{
public:

    /// @brief Full text of documents in one page. It will be used for
    /// caching in BA when "DocumentClick" query occurs.
    /// @see pageInfo_
    ///
    /// @details
    /// ProtoType : fullTextOfDocumentInPage_[DISPLAY PROPERTY Order][DOC Order]
    /// - DISPLAY PROPERTY Order : The sequence which follows the order in displayPropertyList_
    /// - DOC Order : The sequence which follows the order of docIdList
    std::vector<std::vector<PropertyValue::PropertyValueStrType> >  fullTextOfDocumentInPage_;

    /// Raw Text of Document is used for generating resultXML of "KeywordSearch" query.
    /// And the first index is following the sequence of the displayPropertyList in KeywordSearchActionItem.
    /// The second index is following the sequence of the rankedDocIdList_;
    std::vector<std::vector<PropertyValue::PropertyValueStrType> >  snippetTextOfDocumentInPage_;


    /// Raw Text of Document is used for generating resultXML of "KeywordSearch" query.
    /// And the first index is following the sequence of the
    /// displayPropertyList(which is set to generate summary) in KeywordSearchActionItem.
    /// The second index is following the sequence of the rankedDocIdList_;
    std::vector<std::vector<PropertyValue::PropertyValueStrType> >  rawTextOfSummaryInPage_;

    /// internal IDs of the documents
    std::vector<docid_t> idList_;

    /// corresponding workerids for each id. (no need to be serialized)
    std::vector<workerid_t> workeridList_;

    void getWIdList(std::vector<sf1r::wdocid_t>& widList) const
    {
        if (workeridList_.size() <= 0)
        {
            widList.assign(idList_.begin(), idList_.end());
            return;
        }

        widList.resize(idList_.size());
        for (size_t i = 0; i < idList_.size(); i++)
        {
            widList[i] = net::aggregator::Util::GetWDocId(workeridList_[i], idList_[i]);
        }
    }

    //LOG: changed the names for consistentcy with KeywordResultItem
    //DATA_IO_LOAD_SAVE(RawTextResultFromSIA,
    //        &fullTextOfDocumentInOnePage_&rawTextOfDocument_&rawTextOfSummary_

    DATA_IO_LOAD_SAVE(RawTextResultFromSIA,
            &fullTextOfDocumentInPage_&snippetTextOfDocumentInPage_&rawTextOfSummaryInPage_&idList_
            &errno_&error_
            );

    MSGPACK_DEFINE(fullTextOfDocumentInPage_, snippetTextOfDocumentInPage_, rawTextOfSummaryInPage_,
            idList_, errno_, error_);
}; // end - class RawTextResultFromSIA


class RawTextResultFromMIA : public RawTextResultFromSIA // Log : 2011.07.27
{
public:

    std::vector<count_t> numberOfDuplicatedDocs_;

    std::vector<count_t> numberOfSimilarDocs_;



    //LOG: changed the names for consistentcy with KeywordResultItem
    //DATA_IO_LOAD_SAVE(RawTextResultFromSIA,
    //        &fullTextOfDocumentInOnePage_&rawTextOfDocument_&rawTextOfSummary_

    DATA_IO_LOAD_SAVE(RawTextResultFromMIA,
            &fullTextOfDocumentInPage_&snippetTextOfDocumentInPage_&rawTextOfSummaryInPage_&idList_
            &numberOfDuplicatedDocs_&numberOfSimilarDocs_
            &errno_&error_
            );

    MSGPACK_DEFINE(fullTextOfDocumentInPage_, snippetTextOfDocumentInPage_, rawTextOfSummaryInPage_,
            idList_, numberOfDuplicatedDocs_, numberOfSimilarDocs_, errno_, error_);
}; // end - class RawTextResultFromMIA

} // end - namespace sf1r


#endif // _RESULTTYPE_H_
