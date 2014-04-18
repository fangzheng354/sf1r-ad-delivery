///
/// @file   DocumentManager.cpp
/// @brief  Manages properties and rawtexts
/// @date   2009-10-21 12:16:36
/// @author Deepesh Shrestha, Peiseng Wang,
///

#include "DocumentManager.h"
#include "DocContainer.h"
#include "highlighter/Highlighter.h"
#include "snippet-generation-submanager/SnippetGeneratorSubManager.h"
#include "text-summarization-submanager/TextSummarizationSubManager.h"

#include <configuration-manager/ConfigurationTool.h>
#include <util/profiler/ProfilerGroup.h>
#include <common/SFLogger.h>
#include <common/NumericPropertyTable.h>
#include <common/NumericRangePropertyTable.h>
#include <common/RTypeStringPropTable.h>
#include <la/analyzer/MultiLanguageAnalyzer.h>
#include <am/sequence_file/ssfr.h>

#include <langid/langid.h>

#include <fstream>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/archive/xml_iarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/archive/archive_exception.hpp>
#include <boost/algorithm/string.hpp>

#include <protect/RestrictMacro.h>


namespace sf1r
{

const std::string DocumentManager::ACL_FILE = "ACLTable";
const std::string DocumentManager::PROPERTY_LENGTH_FILE = "PropertyLengthDb.xml";
const std::string DocumentManager::PROPERTY_BLOCK_SUFFIX = ".blocks";

namespace
{
const std::string DOCID("DOCID");
const std::string DATE("DATE");
}

DocumentManager::DocumentManager(
        const std::string& path,
        const IndexBundleSchema& indexSchema,
        const izenelib::util::UString::EncodingType encodingType,
        size_t documentCacheNum)
    : path_(path)
    , delfilter_count_(0)
    , documentCache_(100)
    , indexSchema_(indexSchema)
    , encodingType_(encodingType)
    , maxSnippetLength_(200)
{
    propertyValueTable_ = new DocContainer(path);
    propertyValueTable_->open();
    //buildPropertyIdMapper_();
    restorePropertyLengthDb_();
    loadDelFilter_();
    //aclTable_.open();
    snippetGenerator_ = new SnippetGeneratorSubManager;
    highlighter_ = new Highlighter;

    // Normal index
    for (IndexBundleSchema::const_iterator it = indexSchema_.begin();
            it != indexSchema_.end(); ++it)
    {
        if(it->isRTypeString())
        {
            initRTypeStringPropTable(it->getName());
        }
        else if (it->isRTypeNumeric())
        {
            initNumericPropertyTable_(it->getName(), it->getType(), it->getIsRange());
        }
    }
}

void DocumentManager::setZambeziConfig(const ZambeziConfig& zambeziConfig)
{
    zambeziConfig_ = zambeziConfig;

    for (IndexBundleSchema::const_iterator it = zambeziConfig_.zambeziIndexSchema.begin();
         it != zambeziConfig_.zambeziIndexSchema.end(); ++it)
    {
        if(it->isRTypeString())
        {
            initRTypeStringPropTable(it->getName());
        }
        else if (it->isRTypeNumeric())
        {
            initNumericPropertyTable_(it->getName(), it->getType(), it->getIsRange());
        }
    }

    buildPropertyIdMapper_();
}

DocumentManager::~DocumentManager()
{
    flush();

    if (propertyValueTable_) delete propertyValueTable_;
    if (snippetGenerator_) delete snippetGenerator_;
    if (highlighter_) delete highlighter_;
}

bool DocumentManager::flush()
{
    propertyValueTable_->flush();
    for (RTypeStringPropTableMap::iterator it = rtype_string_proptable_.begin();
            it != rtype_string_proptable_.end(); ++it)
    {
        it->second->flush();
    }

    for (NumericPropertyTableMap::iterator it = numericPropertyTables_.begin();
            it != numericPropertyTables_.end(); ++it)
    {
        it->second->flush();
    }
    saveDelFilter_();
    return savePropertyLengthDb_();
}

bool DocumentManager::insertDocument(const Document& document)
{

    typedef Document::property_const_iterator iterator;
    for (iterator it = document.propertyBegin(), itEnd = document.propertyEnd(); it
            != itEnd; ++it)
    {
        const propertyid_t* pid = propertyIdMapper_.findIdByValue(it->first);
        if (!pid)
        {
            continue;
        }

        const Document::doc_prop_value_strtype* stringValue =
            izenelib::get<Document::doc_prop_value_strtype>(&it->second);
        if (stringValue)
        {
            if (propertyLengthDb_.size() <= *pid)
            {
                boost::unique_lock<boost::shared_mutex> lock(shared_mutex_);
                if (propertyLengthDb_.size() <= *pid)
                    propertyLengthDb_.resize(*pid + 1);
            }
            boost::shared_lock<boost::shared_mutex> lock(shared_mutex_);
            propertyLengthDb_[*pid] += stringValue->length();
        }
    }

    if (!propertyValueTable_->insert(document.getId(), document))
    {
        return false;
    }

    return true;
}

bool DocumentManager::updateDocument(const Document& document)
{
    if (propertyValueTable_->update(document.getId(), document))
    {
        documentCache_.del(document.getId());
        return true;
    }

    return false;
}

bool DocumentManager::updatePartialDocument(const Document& document) // ok, right ...
{
    docid_t docId = document.getId();
    Document oldDoc;

    if (!getDocument(docId, oldDoc))
    {
        return false;
    }

    typedef Document::property_const_iterator iterator;

    for (iterator it = document.propertyBegin(), itEnd = document.propertyEnd(); it
            != itEnd; ++it)
    {
        const propertyid_t* pid = propertyIdMapper_.findIdByValue(it->first);
        if (!pid)
        {
            // not in config, skip
            continue;
        }

        if (! boost::iequals(it->first,DOCID) && ! boost::iequals(it->first,DATE))
        {
            oldDoc.updateProperty(it->first, it->second);
        }
    }

    return updateDocument(oldDoc);
}

bool DocumentManager::isDeleted(docid_t docId) const
{
    if (docId-- == 0) return false;

    size_t segment = docId / DELFILTER_SEGMENT_SIZE;
    if (delfilter_[segment].empty()) return false;
    size_t offset = docId % DELFILTER_SEGMENT_SIZE;

    return delfilter_[segment].test(offset);
}

bool DocumentManager::removeDocument(docid_t docId)
{
    if (docId-- == 0) return false;

    size_t segment = docId / DELFILTER_SEGMENT_SIZE;
    size_t offset = docId % DELFILTER_SEGMENT_SIZE;
    for (int i = segment; i >= 0 && delfilter_[i].empty(); --i)
    {
        delfilter_[i].resize(DELFILTER_SEGMENT_SIZE);
    }

    if (delfilter_[segment].test(offset))
        return false;

    delfilter_[segment].set(offset);
    ++delfilter_count_;
    documentCache_.del(docId + 1);

    return true;
}

std::size_t DocumentManager::getTotalPropertyLength(const std::string& property)
{
    boost::unordered_map<std::string, unsigned int>::const_iterator iter =
        propertyAliasMap_.find(property);

    boost::shared_lock<boost::shared_mutex> lock(shared_mutex_);
    if (iter != propertyAliasMap_.end() && iter->second < propertyLengthDb_.size())
        return propertyLengthDb_[ iter->second ];
    else
    {
        const unsigned int* id = propertyIdMapper_.findIdByValue(property);
        if (id != 0 && *id < propertyLengthDb_.size())
            return propertyLengthDb_[*id];
    }

    return 0;
}

bool DocumentManager::getPropertyValue(
        docid_t docId,
        const std::string& propertyName,
        PropertyValue& result)
{
    Document doc;
    const std::string* realPropertyName = &propertyName;

    //restore alias property name to original
    boost::unordered_map<std::string, unsigned int>::iterator aIter =
        propertyAliasMap_.find(propertyName);
    if (aIter != propertyAliasMap_.end())
    {
        realPropertyName = propertyIdMapper_.findValueById(aIter->second);
    }

    NumericPropertyTableMap::const_iterator it = numericPropertyTables_.find(*realPropertyName);
    if (it != numericPropertyTables_.end())
    {
        std::string tempStr;
        if (!it->second->getStringValue(docId, tempStr))
        {
//          return false;
        }
        result = str_to_propstr(tempStr, encodingType_);
        return true;
    }

    RTypeStringPropTableMap::const_iterator rtype_table_cit = rtype_string_proptable_.find(*realPropertyName);
    if( rtype_table_cit != rtype_string_proptable_.end() )
    {
        std::string tempStr;
        rtype_table_cit->second->getRTypeString(docId, tempStr);
        result = str_to_propstr(tempStr, encodingType_);
        return true;
    }

    if (documentCache_.getValue(docId, doc))
    {
        result = doc.property(*realPropertyName);
    }
    else
    {
        if (getDocument(docId, doc) == false)
            return false;
        documentCache_.insertValue(docId, doc);
        result = doc.property(*realPropertyName);
    }
//  if (getDocument(docId, doc))
//      result = doc.property(*realPropertyName);
    return true;
}

bool DocumentManager::getDocument(docid_t docId, Document& document, bool forceget)
{
    CREATE_SCOPED_PROFILER ( getDocument, "DocumentManager", "DocumentManager::getDocument");
    return (forceget || !isDeleted(docId)) && propertyValueTable_->get(docId, document);
}

void DocumentManager::getRTypePropertiesForDocument(docid_t docId, Document& document)
{
    for (NumericPropertyTableMap::const_iterator it = numericPropertyTables_.begin();
            it != numericPropertyTables_.end(); ++it)
    {
        std::string tempStr;
        if (it->second->getStringValue(docId, tempStr))
            document.property(it->first) = str_to_propstr(tempStr, encodingType_);
    }
    for(RTypeStringPropTableMap::const_iterator cit = rtype_string_proptable_.begin();
        cit != rtype_string_proptable_.end(); ++cit)
    {
        std::string tempStr;
        if(cit->second->getRTypeString(docId, tempStr))
            document.property(cit->first) = str_to_propstr(tempStr, encodingType_);
    }
}

bool DocumentManager::existDocument(docid_t docId)
{
    return propertyValueTable_->exist(docId);
}

bool DocumentManager::getDocumentByCache(
        docid_t docId,
        Document& document,
        bool forceget)
{
    if (documentCache_.getValue(docId, document))
    {
        return true;
    }
    if ((forceget || !isDeleted(docId)) && propertyValueTable_->get(docId, document))
    {
        documentCache_.insertValue(docId, document);
        return true;
    }
    return false;
}

bool DocumentManager::getDocuments(
        const std::vector<unsigned int>& ids,
        std::vector<Document>& docs,
        bool forceget)
{
    docs.resize(ids.size());
    bool ret = true;
    for (size_t i=0; i<ids.size(); i++)
    {
        ret &= getDocumentByCache(ids[i], docs[i], forceget);
    }
    return ret;
}

docid_t DocumentManager::getMaxDocId() const
{
    return propertyValueTable_->getMaxDocId();
}

uint32_t DocumentManager::getNumDocs()
{
    return getMaxDocId() - delfilter_count_;
}

bool DocumentManager::getDeletedDocIdList(std::vector<docid_t>& docid_list) const
{
    docid_list.clear();
    docid_list.reserve(delfilter_count_);

    for (size_t segment = 0; segment < 32 && !delfilter_[segment].empty(); ++segment)
    {
        docid_t base = segment * DELFILTER_SEGMENT_SIZE + 1;
        DelFilterType::size_type find = delfilter_[segment].find_first();
        while (find != DelFilterType::npos)
        {
            docid_list.push_back(base + (docid_t)find);
            find = delfilter_[segment].find_next(find);
        }
    }

    return true;
}

bool DocumentManager::loadDelFilter_()
{
    const std::string filter_file = (boost::filesystem::path(path_) / "del_filter").string();
    std::vector<DelFilterBlockType> filter_data;

    if (!izenelib::am::ssf::Util<>::Load(filter_file, filter_data))
        return false;

    size_t segsize = DELFILTER_SEGMENT_SIZE / sizeof(DelFilterBlockType) / 8;
    size_t segment = filter_data.size() / segsize;
    std::vector<DelFilterBlockType>::const_iterator it = filter_data.begin();
    for (size_t i = 0; i < segment; ++i, it += segsize)
    {
        delfilter_[i].clear();
        delfilter_[i].append(it, it + segsize);
    }

    return true;
}

bool DocumentManager::saveDelFilter_() const
{
    size_t segment = 0;
    while (segment < 32 && !delfilter_[segment].empty()) ++segment;

    size_t segsize = DELFILTER_SEGMENT_SIZE / sizeof(DelFilterBlockType) / 8;
    std::vector<DelFilterBlockType> filter_data(segment * segsize);

    std::vector<DelFilterBlockType>::iterator it = filter_data.begin();
    for (size_t i = 0; i < segment; ++i, it += segsize)
    {
        boost::to_block_range(delfilter_[i], it);
    }

    const std::string filter_file = (boost::filesystem::path(path_) / "del_filter").string();
    if (!izenelib::am::ssf::Util<>::Save(filter_file, filter_data))
    {
        std::cout << "DocumentManager::saveDelFilter_() failed" << std::endl;
        return false;
    }

    return true;
}

void DocumentManager::buildPropertyIdMapper_()
{
    config_tool::PROPERTY_ALIAS_MAP_T propertyAliasMap;
    config_tool::buildPropertyAliasMap(indexSchema_, propertyAliasMap); // for the second map ...

    for (IndexBundleSchema::const_iterator it = indexSchema_.begin(), itEnd = indexSchema_.end(); //
            it != itEnd; ++it)
    {
        propertyid_t originalPropertyId(0), originalBlockId(0);

        bool hasSummary(it->getIsSummary());
        bool hasSnippet(it->getIsSnippet());

        if (hasSnippet || hasSummary)
        {
            originalBlockId = propertyIdMapper_.insert(it->getName()
                              + PROPERTY_BLOCK_SUFFIX);

            boost::unordered_map<std::string, unsigned int>::iterator dispIter;
            dispIter = displayLengthMap_.find(it->getName());
            if (dispIter == displayLengthMap_.end())
            {
                unsigned int dispLength = it->getDisplayLength();
                if ((dispLength < 50) || (dispLength > 250)) //50, 250 as min and max DisplayLength
                    dispLength = maxSnippetLength_;
                displayLengthMap_.insert(std::make_pair(it->getName(),
                                                        dispLength));
            }
        }
        originalPropertyId = propertyIdMapper_.insert(it->getName()); // only this is ok;..

        // For alias property
        config_tool::PROPERTY_ALIAS_MAP_T::iterator aliasIter =
            propertyAliasMap.find(it->getName());
        if (aliasIter != propertyAliasMap.end())
        {

            for (std::vector<PropertyConfig>::iterator vecIter =
                    aliasIter->second.begin(); vecIter
                    != aliasIter->second.end(); vecIter++)
            {
                if (hasSnippet)
                {

                    propertyAliasMap_.insert(make_pair(vecIter->getName()
                                                       + PROPERTY_BLOCK_SUFFIX, originalBlockId));

                }
                propertyAliasMap_.insert(make_pair(vecIter->getName(),
                                                   originalPropertyId)) ;
            }
        }
    }

    // for zambezi index;
    for (IndexBundleSchema::const_iterator it = zambeziConfig_.zambeziIndexSchema.begin(),
        itEnd = zambeziConfig_.zambeziIndexSchema.end(); it != itEnd; ++it)
    {
        propertyIdMapper_.insert(it->getName());
    }

} // end - buildPropertyIdMapper_()

bool DocumentManager::savePropertyLengthDb_() const
{
    try
    {
        const std::string kDbPath = path_ + PROPERTY_LENGTH_FILE;
        std::ofstream ofs(kDbPath.c_str());
        if (ofs)
        {
            boost::archive::xml_oarchive oa(ofs);
            oa << boost::serialization::make_nvp("PropertyLength", propertyLengthDb_);
        }

        return ofs;
    }
    catch (boost::archive::archive_exception& e)
    {
        DLOG(ERROR)<<"Serialization Error while saving property length."<<e.what()<<endl;
        return false;
    }
}

bool DocumentManager::restorePropertyLengthDb_()
{
    try
    {
        const std::string kDbPath = path_ + PROPERTY_LENGTH_FILE;
        std::ifstream ifs(kDbPath.c_str());
        if (ifs)
        {
            boost::archive::xml_iarchive ia(ifs);
            ia >> boost::serialization::make_nvp("PropertyLength", propertyLengthDb_);
        }
        return ifs;
    }
    catch (boost::archive::archive_exception& e)
    {
        DLOG(ERROR)<<"Serialization Error while restoring property length."<<e.what()<<endl;
        propertyLengthDb_.clear();
        return false;
    }
}

bool DocumentManager::getRawTextOfDocuments(
        const std::vector<docid_t>& docIdList, const string& propertyName,
        const bool summaryOn, const unsigned int summaryNum,
        const unsigned int option,
        const std::vector<izenelib::util::UString>& queryTerms,
        std::vector<Document::doc_prop_value_strtype>& outSnippetList,
        std::vector<Document::doc_prop_value_strtype>& outRawSummaryList,
        std::vector<Document::doc_prop_value_strtype>& outFullTextList)
{
    try
    {
        unsigned int docListSize = docIdList.size();
        std::vector<Document::doc_prop_value_strtype> snippetList(docListSize);
        std::vector<Document::doc_prop_value_strtype> rawSummaryList(docListSize);
        std::vector<Document::doc_prop_value_strtype> fullTextList(docListSize);

        Document::doc_prop_value_strtype rawText; // raw text
        izenelib::util::UString rawUText; // raw text
        Document::doc_prop_value_strtype result; // output variable to store return value
        izenelib::util::UString resultU;

        bool ret = false;
        for (unsigned int listId = 0; listId != docListSize; ++listId)
        {
            docid_t docId = docIdList[listId];
            result.clear();
            resultU.clear();

            if (!getPropertyValue(docId, propertyName, rawText))
                continue;

            rawUText = propstr_to_ustr(rawText, encodingType_);
            fullTextList[listId] = rawText;

            std::string sentenceProperty = propertyName + PROPERTY_BLOCK_SUFFIX;
            std::vector<CharacterOffset> sentenceOffsets;
            if (!getPropertyValue(docId, sentenceProperty, sentenceOffsets))
                continue;

            ret = true;

            maxSnippetLength_ = getDisplayLength_(propertyName);
            processOptionForRawText(option, queryTerms, rawUText,
                                    sentenceOffsets, resultU);

            result = ustr_to_propstr(resultU);
            if (result.size() > 0)
                snippetList[listId] = result;
            else
                snippetList[listId] = rawText;

            //process only if summary is ON
            unsigned int numSentences = summaryNum <= 0 ? 1 : summaryNum;
            if (summaryOn)
            {
                izenelib::util::UString summary;
                getSummary(rawUText, sentenceOffsets, numSentences,
                           option, queryTerms, summary);

                rawSummaryList[listId] = ustr_to_propstr(summary);
            }
        }
        outSnippetList.swap(snippetList);
        outFullTextList.swap(fullTextList);
        if (summaryOn)
            outRawSummaryList.swap(rawSummaryList);

        return ret;
    }
    catch (std::exception& e)
    {
        return false;
    }
}

bool DocumentManager::getRawTextOfOneDocument(
        const docid_t docId,
        Document& document,
        const string& propertyName,
        const unsigned int option,
        const std::vector<izenelib::util::UString>& queryTerms,
        Document::doc_prop_value_strtype& outSnippet,
        Document::doc_prop_value_strtype& rawText)
{
    rawText.clear();
    NumericPropertyTableMap::const_iterator it = numericPropertyTables_.find(propertyName);
    RTypeStringPropTableMap::const_iterator rtype_table_cit = rtype_string_proptable_.find(propertyName);
    if (it != numericPropertyTables_.end())
    {
        std::string tempStr;
        if (!it->second->getStringValue(docId, tempStr))
        {
//          return false;
        }
        rawText = str_to_propstr(tempStr, encodingType_);
    }
    else if( rtype_table_cit != rtype_string_proptable_.end() )
    {
        std::string tempStr;
        rtype_table_cit->second->getRTypeString(docId, tempStr);
        rawText = str_to_propstr(tempStr, encodingType_);
    }
    else
    {
        Document::doc_prop_value_strtype propValue;
        if(document.getProperty(propertyName,propValue))
            rawText = propValue;
    }

    if (rawText.empty())
    {
        //DLOG(ERROR)<<"No RawText For This Property. Property Name "<<propertyName<<endl;
        return true;
    }

    std::vector<CharacterOffset> sentenceOffsets;
    if (option > 1)
    {
        if (!getPropertyValue(docId, propertyName + PROPERTY_BLOCK_SUFFIX,
                              sentenceOffsets))
        {
            outSnippet = rawText;
            return false;
        }
    }

    maxSnippetLength_ = getDisplayLength_(propertyName);
    izenelib::util::UString tempustr;
    processOptionForRawText(option, queryTerms, propstr_to_ustr(rawText), sentenceOffsets,
                            tempustr);

    outSnippet = ustr_to_propstr(tempustr);
    //put raw text to outSnippet if it is empty
    if (outSnippet.size() <= 0)
        outSnippet = rawText;

    return true;

}

bool DocumentManager::processOptionForRawText(
        const unsigned int option,
        const std::vector<izenelib::util::UString>& queryTerms,
        const izenelib::util::UString& rawText,
        const std::vector<CharacterOffset>& sentenceOffsets,
        izenelib::util::UString& result)
{
    switch (option)
    {
    case X_RAWTEXT:
        result = rawText;
        break;

    case O_RAWTEXT:
        highlighter_->getHighlightedText(rawText, queryTerms, encodingType_, result);
        break;

    case X_SNIPPET:
        snippetGenerator_->getSnippet(rawText, sentenceOffsets, queryTerms,
                                      maxSnippetLength_, false, encodingType_, result);
        break;

    case O_SNIPPET:
        snippetGenerator_->getSnippet(rawText, sentenceOffsets, queryTerms,
                                      maxSnippetLength_, true, encodingType_, result);
        break;
    }
    return true;
}

bool DocumentManager::getSummary(
        const izenelib::util::UString& rawText,
        const std::vector<CharacterOffset>& sentenceOffsets,
        unsigned int numSentences,
        const unsigned int option,
        const std::vector<izenelib::util::UString>& queryTerms,
        izenelib::util::UString& summary)
{
    uint32_t offsetIndex = 1;
    uint32_t summaryCount = 0;
    uint32_t indexedSummaryCount = 0;

    //if stored summaries is less than the requested summary number
    if (numSentences > sentenceOffsets[indexedSummaryCount])
        numSentences = sentenceOffsets[indexedSummaryCount];

    while (offsetIndex < sentenceOffsets[indexedSummaryCount] * 2 && summaryCount < numSentences)
    {
        uint32_t length = sentenceOffsets[offsetIndex + 1]
                          - sentenceOffsets[offsetIndex];
        izenelib::util::UString result;
        izenelib::util::UString sentence;
        rawText.substr(sentence, sentenceOffsets[offsetIndex], length);
        offsetIndex += 2;
        switch (option)
        {
        case X_RAWTEXT: //don't highlight
        case X_SNIPPET:
            result = sentence;
            break;
        case O_RAWTEXT: //Do highlight
        case O_SNIPPET:
            if (highlighter_->getHighlightedText(sentence, queryTerms,
                                                encodingType_, result) == false)
            {
                result = sentence;
            }
            break;
        }
        if (result.length()> 0)
            summary += result;

        ++summaryCount;
    }
    return true;
}

unsigned int DocumentManager::getDisplayLength_(const string& propertyName)
{
    boost::unordered_map< std::string, unsigned int>::iterator dispIter =
        displayLengthMap_.find(propertyName);
    if (dispIter != displayLengthMap_.end())
        return dispIter->second;

    return 0;
}

bool DocumentManager::highlightText(
        const izenelib::util::UString& text,
        const std::vector<izenelib::util::UString> queryTerms,
        izenelib::util::UString& outText)
{
    izenelib::util::UString highlightedText;
    highlighter_->getHighlightedText(text, queryTerms, encodingType_,
                                    highlightedText);
    outText.swap(highlightedText);
    return true;
}

boost::shared_ptr<NumericPropertyTableBase>& DocumentManager::getNumericPropertyTable(const std::string& propertyName)
{
    static boost::shared_ptr<NumericPropertyTableBase> emptyNumericPropertyTable;
    NumericPropertyTableMap::iterator it = numericPropertyTables_.find(propertyName);
    if (it != numericPropertyTables_.end())
        return it->second;
    else
        return emptyNumericPropertyTable;
}

void DocumentManager::initNumericPropertyTable_(
        const std::string& propertyName,
        PropertyDataType propertyType,
        bool isRange)
{
    if (numericPropertyTables_.find(propertyName) != numericPropertyTables_.end())
        return;

    boost::shared_ptr<NumericPropertyTableBase>& numericPropertyTable = numericPropertyTables_[propertyName];
    switch (propertyType)
    {
    case INT8_PROPERTY_TYPE:
        if (isRange)
            numericPropertyTable.reset(new NumericRangePropertyTable<int8_t>(propertyType));
        else
            numericPropertyTable.reset(new NumericPropertyTable<int8_t>(propertyType));
        break;

    case INT16_PROPERTY_TYPE:
        if (isRange)
            numericPropertyTable.reset(new NumericRangePropertyTable<int16_t>(propertyType));
        else
            numericPropertyTable.reset(new NumericPropertyTable<int16_t>(propertyType));
        break;

    case INT32_PROPERTY_TYPE:
        if (isRange)
            numericPropertyTable.reset(new NumericRangePropertyTable<int32_t>(propertyType));
        else
            numericPropertyTable.reset(new NumericPropertyTable<int32_t>(propertyType));
        break;

    case INT64_PROPERTY_TYPE:
    case DATETIME_PROPERTY_TYPE:
        if (isRange)
            numericPropertyTable.reset(new NumericRangePropertyTable<int64_t>(propertyType));
        else
            numericPropertyTable.reset(new NumericPropertyTable<int64_t>(propertyType));
        break;

    case FLOAT_PROPERTY_TYPE:
        if (isRange)
            numericPropertyTable.reset(new NumericRangePropertyTable<float>(propertyType));
        else
            numericPropertyTable.reset(new NumericPropertyTable<float>(propertyType));
        break;

    case DOUBLE_PROPERTY_TYPE:
        if (isRange)
            numericPropertyTable.reset(new NumericRangePropertyTable<double>(propertyType));
        else
            numericPropertyTable.reset(new NumericPropertyTable<double>(propertyType));
        break;

    default:
        numericPropertyTables_.erase(propertyName);
        return;
    }
    numericPropertyTable->init(path_ + propertyName + ".rtype_data");
}

boost::shared_ptr<RTypeStringPropTable>& DocumentManager::getRTypeStringPropTable(const std::string& propertyName)
{
    static boost::shared_ptr<RTypeStringPropTable> emptyPropertyTable;
    RTypeStringPropTableMap::iterator it = rtype_string_proptable_.find(propertyName);
    if (it != rtype_string_proptable_.end())
        return it->second;
    else
        return emptyPropertyTable;
}

void DocumentManager::initRTypeStringPropTable(
        const std::string& propertyName)
{
    if (rtype_string_proptable_.find(propertyName) != rtype_string_proptable_.end())
        return;

    boost::shared_ptr<RTypeStringPropTable>& rtype_string_prop = rtype_string_proptable_[propertyName];
    rtype_string_prop.reset(new RTypeStringPropTable(STRING_PROPERTY_TYPE));
    rtype_string_prop->init(path_ + propertyName + ".rtypestring_data");
}

void DocumentManager::moveRTypeValues(docid_t oldId, docid_t newId)
{
    for (NumericPropertyTableMap::iterator it = numericPropertyTables_.begin();
            it != numericPropertyTables_.end(); ++it)
    {
        it->second->copyValue(oldId, newId);
    }
    for(RTypeStringPropTableMap::iterator rtype_it = rtype_string_proptable_.begin();
        rtype_it != rtype_string_proptable_.end(); ++rtype_it)
    {
        rtype_it->second->copyValue(oldId, newId);
    }
}

// this is used for Rebuild collection ...

void DocumentManager::copyRTypeValues(
    boost::shared_ptr<DocumentManager>& source,
    docid_t from, docid_t to)
{
    std::set<std::string> doneProperty;
    for (IndexBundleSchema::const_iterator it = indexSchema_.begin();
            it != indexSchema_.end(); ++it)
    {
        if(it->isRTypeString())
        {
            doneProperty.insert(it->getName());
            std::string fieldValue;
            boost::shared_ptr<RTypeStringPropTable> sourceTable = source->getRTypeStringPropTable(it->getName());
            if(!sourceTable) continue;
            boost::shared_ptr<RTypeStringPropTable> rtypeStringPropertyTable = rtype_string_proptable_[it->getName()];
            bool ret = sourceTable->getRTypeString(from, fieldValue);
            if(ret) rtypeStringPropertyTable->updateRTypeString(to, fieldValue);
        }
        else if (it->isRTypeNumeric())
        {
            doneProperty.insert(it->getName());
            boost::shared_ptr<NumericPropertyTableBase> sourceTable = source->getNumericPropertyTable(it->getName());
            if(!sourceTable) continue;
            boost::shared_ptr<NumericPropertyTableBase> numericPropertyTable = numericPropertyTables_[it->getName()];
            if( (it->getType() == DATETIME_PROPERTY_TYPE) &&
                (it->getIsFilter() && !it->getIsMultiValue()) )
            {
                time_t fieldValue;
                bool ret = sourceTable->getInt64Value(from, fieldValue);
                if(ret) numericPropertyTable->setInt64Value(to, fieldValue);
            }
            else
            {
                std::string fieldValue;
                bool ret = sourceTable->getStringValue(from, fieldValue);
                if(ret) numericPropertyTable->setStringValue(to, fieldValue);
            }
        }
    }

    // for zambezi index
    for (IndexBundleSchema::const_iterator it = zambeziConfig_.zambeziIndexSchema.begin();
            it != zambeziConfig_.zambeziIndexSchema.end(); ++it)
    {
        if (it->isRTypeNumeric())
        {
            if (!doneProperty.insert(it->getName()).second)
                continue;

            boost::shared_ptr<NumericPropertyTableBase> sourceTable = source->getNumericPropertyTable(it->getName());
            if(!sourceTable) continue;
            boost::shared_ptr<NumericPropertyTableBase> numericPropertyTable = numericPropertyTables_[it->getName()];
            if( (it->getType() == DATETIME_PROPERTY_TYPE) &&
                (it->getIsFilter() && !it->getIsMultiValue()) )
            {
                time_t fieldValue;
                bool ret = sourceTable->getInt64Value(from, fieldValue);
                if(ret) numericPropertyTable->setInt64Value(to, fieldValue);
            }
            else
            {
                std::string fieldValue;
                bool ret = sourceTable->getStringValue(from, fieldValue);
                if(ret) numericPropertyTable->setStringValue(to, fieldValue);
            }
        }
    }
}

DocumentManager::NumericPropertyTableMap& DocumentManager::getNumericPropertyTableMap()
{
    return numericPropertyTables_;
}

DocumentManager::RTypeStringPropTableMap& DocumentManager::getRTypeStringPropTableMap()
{
    return rtype_string_proptable_;
}

}
