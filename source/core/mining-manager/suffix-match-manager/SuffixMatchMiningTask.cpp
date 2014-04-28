#include "SuffixMatchMiningTask.hpp"
#include <document-manager/DocumentManager.h>
#include <boost/filesystem.hpp>
#include <glog/logging.h>
#include <icma/icma.h>
#include <la-manager/KNlpWrapper.h>
#include <la-manager/LAPool.h>
#include <mining-manager/util/split_ustr.h>
#include <util/ustring/UString.h>
#include "FilterManager.h"
#include "FMIndexManager.h"
#include <fstream>
#include <boost/lexical_cast.hpp>
namespace sf1r
{

SuffixMatchMiningTask::SuffixMatchMiningTask(
        boost::shared_ptr<DocumentManager>& document_manager,
        boost::shared_ptr<FMIndexManager>& fmi_manager,
        boost::shared_ptr<FilterManager>& filter_manager,
        std::string data_root_path,
        boost::shared_mutex& mutex)
    : isRtypeIncremental_(false)
    , document_manager_(document_manager)
    , fmi_manager_(fmi_manager)
    , filter_manager_(filter_manager)
    , data_root_path_(data_root_path)
    , is_incrememtalTask_(false)
    , mutex_(mutex)
{
}

SuffixMatchMiningTask::~SuffixMatchMiningTask()
{

}

bool SuffixMatchMiningTask::preProcess(int64_t timestamp)
{
    if (!is_incrememtalTask_)
    {
        new_filter_manager.reset(new FilterManager(document_manager_, filter_manager_->getGroupManager(), data_root_path_,
                    filter_manager_->getAttrManager(), filter_manager_->getNumericTableBuilder()));
        new_filter_manager->copyPropertyInfo(filter_manager_);
        new_filter_manager->generatePropertyId();

        new_fmi_manager.reset(new FMIndexManager(data_root_path_,
                                                 document_manager_,
                                                 new_filter_manager,
                                                 fmi_manager_->getFuzzyNormalizer()));

        std::vector<std::string> properties;
        VirtualConfig virtulProperty;
        for (int i = 0; i < FMIndexManager::FM_TYPE_COUNT; ++i)
        {
            fmi_manager_->getProperties(properties, (FMIndexManager::PropertyFMType)i);
            fmi_manager_->getVirtualProperty(virtulProperty);
            new_fmi_manager->addProperties(properties, (FMIndexManager::PropertyFMType)i);
            new_fmi_manager->setVirtualProperty(virtulProperty);
            std::vector<std::string>().swap(properties);
        }

        size_t last_docid = fmi_manager_ ? fmi_manager_->docCount() : 0;
        need_rebuild = false;

        std::vector<uint32_t> del_docid_list;
        document_manager_->getDeletedDocIdList(del_docid_list);

        if (last_docid == document_manager_->getMaxDocId()) 
        {
            // check if there is any new deleted doc.
            std::vector<size_t> doclen_list(del_docid_list.size(), 0);
            fmi_manager_->getDocLenList(del_docid_list, doclen_list);
            for (size_t i = 0; i < doclen_list.size(); ++i)
            {
                if (doclen_list[i] > 0)
                {
                    need_rebuild = true;
                    break;
                }
            }

            if (!need_rebuild)
            {
                /*
                @brief : in here document_manager_->isThereRtypePro() just means for the -R SCD, 
                because the Rtype doc in -U SCD is not in this flow.
                */
                if (document_manager_->isThereRtypePro())
                {
                    LOG (INFO) << "Update for R-type SCD" << endl;
                    const std::vector<std::pair<int32_t, std::string> >& prop_list = filter_manager_->getProp_list();
                    for (std::vector<std::pair<int32_t, std::string> >::const_iterator i = prop_list.begin(); i != prop_list.end(); ++i)
                    {
                        std::set<string>::iterator iter = document_manager_->RtypeDocidPros_.find((*i).second);
                        if (iter == document_manager_->RtypeDocidPros_.end())
                        {
                            if (!filter_manager_->isNumericProp((*i).second) && !filter_manager_->isDateProp((*i).second))
                            {
                                new_filter_manager->addUnchangedProperty((*i).second);
                                LOG (INFO) << "Add Unchanged property : " << (*i).second << endl;
                            }
                        }
                    }
                }
                else
                {
                    LOG (INFO) << "All filter properties do not need rebuild ... " << endl;
                    return false;
                }
            }
        }
        else
        {
            LOG(INFO) << "old fmi docCount is : " << last_docid << ", document_manager count:" << document_manager_->getMaxDocId();
            need_rebuild = true;
        }

        if (need_rebuild)
        {
            LOG(INFO) << "rebuilding in fm-index is needed.";
        }
        else
        {
            new_fmi_manager->useOldDocCount(fmi_manager_.get());
        }

        bool isInitAndLoad = true;
        if (!need_rebuild) return true;

        if (!new_fmi_manager->initAndLoadOldDocs(fmi_manager_.get()))
        {
            LOG(ERROR) << "fmindex init building failed, must stop. ";
            new_fmi_manager->clearFMIData();
            isInitAndLoad = false;
        }
        if (!isInitAndLoad) return false;
    }
    return true;
}

bool SuffixMatchMiningTask::postProcess()
{
    if (!is_incrememtalTask_)
    {   
        if (need_rebuild && !new_fmi_manager->buildCollectionAfter())
            return false;
        new_filter_manager->setRebuildFlag(filter_manager_.get());
        size_t last_docid = fmi_manager_ ? fmi_manager_->docCount() : 0;

        new_filter_manager->finishBuildStringFilters();
        new_filter_manager->buildFilters(last_docid, document_manager_->getMaxDocId());
        
        new_fmi_manager->setFilterList(new_filter_manager->getFilterList());

        LOG(INFO) << "building filter data finished";

        new_fmi_manager->buildLessDVProperties();
        new_fmi_manager->buildExternalFilter(); //although here is empty ... but it will swap Unchanged Filter ...
        
        {
            WriteLock lock(mutex_);
            if (!need_rebuild)
            {
                // no rebuilding, so just take the owner of old data.
                LOG(INFO) << "no rebuild need, just swap data for common properties.";
                new_fmi_manager->swapCommonProperties(fmi_manager_.get());
                new_fmi_manager->swapUnchangedFilter(fmi_manager_.get());
                new_filter_manager->swapUnchangedFilter(filter_manager_.get());
                new_filter_manager->clearUnchangedProperties();
            }

            fmi_manager_.swap(new_fmi_manager);
            filter_manager_.swap(new_filter_manager);
            
            filter_manager_->clearRebuildFlag();
            new_fmi_manager.reset();
            new_filter_manager.reset();
        }
        LOG(INFO) << "saving fm-index data";
        fmi_manager_->saveAll();
        filter_manager_->saveFilterId();
        filter_manager_->clearFilterList();
        LOG(INFO) << "building fm-index finished";
    }

    return true;
}

bool SuffixMatchMiningTask::buildDocument(docid_t docID, const Document& doc)
{
    if (!is_incrememtalTask_ && !isRtypeIncremental_)
    {
        bool failed = (doc.getId() == 0);
        new_fmi_manager->appendDocsAfter(failed, doc);
        if (!failed)
        {
            new_filter_manager->buildStringFiltersForDoc(docID, doc);
        }
    }
    return true;
}

docid_t SuffixMatchMiningTask::getLastDocId()
{
    return fmi_manager_ ? fmi_manager_->docCount() + 1 : 1;
}

void SuffixMatchMiningTask::setTaskStatus(bool is_incrememtalTask)
{
    is_incrememtalTask_ = is_incrememtalTask;
}

}
