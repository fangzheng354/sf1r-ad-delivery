#include "MergeComparator.h"
#include <net/aggregator/Util.h>

namespace sf1r{



DocumentComparator::DocumentComparator(const KeywordSearchResult& distSearchResult)
{
    const DistKeywordSearchInfo& distSearchInfo = distSearchResult.distSearchInfo_;
    const std::vector<std::pair<std::string , bool> >& sortPropertyList = distSearchInfo.sortPropertyList_;
    if (sortPropertyList.size() <= 0)
        const_cast<std::vector<std::pair<std::string , bool> >&>(sortPropertyList).push_back(std::make_pair("RANK", false));
    std::vector<std::pair<std::string , bool> >::const_iterator iter;
    for (iter = sortPropertyList.begin(); iter != sortPropertyList.end(); ++iter)
    {
        std::string property = iter->first;
        SortPropertyData* pPropertyComparator = new SortPropertyData(iter->first, iter->second);

        //std::cout << "=== merge sort property : " << property << std::endl;
        void* dataList = NULL;
        if (property == "RANK")
        {
            dataList = (void*)(distSearchResult.topKRankScoreList_.data());
            pPropertyComparator->setDataType(SortPropertyData::DATA_TYPE_FLOAT);
        }
        else if (property == "CUSTOM_RANK")
        {
            dataList = (void*)(distSearchResult.topKCustomRankScoreList_.data());
            pPropertyComparator->setDataType(SortPropertyData::DATA_TYPE_FLOAT);
        }
        else if (property == "GEO_DIST")
        {
            dataList = (void*)(distSearchResult.topKGeoDistanceList_.data());
            pPropertyComparator->setDataType(SortPropertyData::DATA_TYPE_FLOAT);
        }
        else
        {
            while (1)
            {
                bool found = false;
                std::vector<std::pair<std::string, std::vector<int32_t> > >::const_iterator it;
                for (it = distSearchInfo.sortPropertyInt32DataList_.begin(); it != distSearchInfo.sortPropertyInt32DataList_.end(); ++it)
                {
                    if (it->first == property)
                    {
                        dataList = (void*)(it->second.data());
                        pPropertyComparator->setDataType(SortPropertyData::DATA_TYPE_INT32);
                        found = true;
                    }
                }
                if (found) break;

                std::vector<std::pair<std::string, std::vector<int64_t> > >::const_iterator itu;
                for (itu = distSearchInfo.sortPropertyInt64DataList_.begin(); itu != distSearchInfo.sortPropertyInt64DataList_.end(); ++itu)
                {
                    if (itu->first == property)
                    {
                        dataList = (void*)(itu->second.data());
                        pPropertyComparator->setDataType(SortPropertyData::DATA_TYPE_INT64);
                        found = true;
                    }
                }
                if (found) break;

                std::vector<std::pair<std::string, std::vector<float> > >::const_iterator itf;
                for (itf = distSearchInfo.sortPropertyFloatDataList_.begin(); itf != distSearchInfo.sortPropertyFloatDataList_.end(); ++itf)
                {
                    if (itf->first == property)
                    {
                        dataList = (void*)(itf->second.data());
                        pPropertyComparator->setDataType(SortPropertyData::DATA_TYPE_FLOAT);
                        found = true;
                    }
                }
                if (found) break;

                std::vector<std::pair<std::string, std::vector<double> > >::const_iterator itd;
                for (itd = distSearchInfo.sortPropertyDoubleDataList_.begin(); itd != distSearchInfo.sortPropertyDoubleDataList_.end(); ++itd)
                {
                    if (itd->first == property)
                    {
                        dataList = (void*)(itd->second.data());
                        pPropertyComparator->setDataType(SortPropertyData::DATA_TYPE_FLOAT);
                        found = true;
                    }
                }
                if (found) break;

                std::vector<std::pair<std::string, std::vector<std::string> > >::const_iterator itstr;
                for (itstr = distSearchInfo.sortPropertyStrDataList_.begin(); itstr != distSearchInfo.sortPropertyStrDataList_.end(); ++itstr)
                {
                    if (itstr->first == property)
                    {
                        dataList = (void*)(itstr->second.data());
                        pPropertyComparator->setDataType(SortPropertyData::DATA_TYPE_STRING);
                        found = true;
                    }
                }

                break;
            }
        }

        if (dataList !=  NULL)
        {
            pPropertyComparator->setDataList(dataList);
            sortProperties_.push_back(pPropertyComparator);
        }
        else
        {
            delete pPropertyComparator;
            //std::cerr << "Merging, no data for sort property " << property <<endl;
        }
    }
}

DocumentComparator::~DocumentComparator()
{
    for (size_t i = 0; i < sortProperties_.size(); i++)
    {
        delete sortProperties_[i];
    }
}

bool greaterThan(DocumentComparator* comp1, size_t idx1, wdocid_t left_docid,
    DocumentComparator* comp2, size_t idx2, wdocid_t right_docid)
{
    for (size_t i = 0; i < comp1->sortProperties_.size(); i++)
    {
        if (i >= comp2->sortProperties_.size())
            break;

        SortPropertyData* pSortProperty1 = comp1->sortProperties_[i];
        SortPropertyData* pSortProperty2 = comp2->sortProperties_[i];

        //std::cout << "comparing property: " << pSortProperty1->getProperty() << std::endl;

        SortPropertyData::DataType dataType1 = pSortProperty1->getDataType();
        SortPropertyData::DataType dataType2 = pSortProperty2->getDataType();
        if (dataType1 != dataType2)
            continue;

        void* dataList1 = pSortProperty1->getDataList();
        void* dataList2 = pSortProperty2->getDataList();

        if (dataType1 == SortPropertyData::DATA_TYPE_INT32)
        {
            int32_t v1 = ((int32_t*)dataList1)[idx1];
            int32_t v2 = ((int32_t*)dataList2)[idx2];
            if (v1 == v2) continue;
            return pSortProperty1->isReverse() ? (v1 < v2) : (v1 > v2);
        }
        else if (dataType1 == SortPropertyData::DATA_TYPE_INT64)
        {
            int64_t v1 = ((int64_t*)dataList1)[idx1];
            int64_t v2 = ((int64_t*)dataList2)[idx2];
            if (v1 == v2) continue;
            return pSortProperty1->isReverse() ? (v1 < v2) : (v1 > v2);
        }
        else if (dataType1 == SortPropertyData::DATA_TYPE_FLOAT)
        {
            float v1 = ((float*)dataList1)[idx1];
            float v2 = ((float*)dataList2)[idx2];
            if (std::fabs(v1 - v2) <= std::numeric_limits<float>::epsilon()) continue;
            return pSortProperty1->isReverse() ? (v1 < v2) : (v1 > v2);
        }
        else if (dataType1 == SortPropertyData::DATA_TYPE_STRING)
        {
            std::string v1 = ((std::string*)dataList1)[idx1];
            std::string v2 = ((std::string*)dataList2)[idx2];
            if (v1 == v2) continue;
            return pSortProperty1->isReverse() ? (v1 < v2) : (v1 > v2);
        }
    }

    //std::cerr << "all sort data is the same, just sort by docid : " << left_docid << " VS " << right_docid << std::endl;
    return net::aggregator::Util::IsNewerDocId(left_docid, right_docid);
}


}
