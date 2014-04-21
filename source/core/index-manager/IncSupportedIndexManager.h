#ifndef SF1V5_INDEX_MANAGER_INCSUPPORTED_INDEXMANAGER_H
#define SF1V5_INDEX_MANAGER_INCSUPPORTED_INDEXMANAGER_H

#include <boost/shared_ptr.hpp>
#include <vector>
#include <common/type_defs.h>
#include <document-manager/DocumentManager.h>
namespace sf1r
{

// Define the interface for the index which will support increment build.
// This kind of index will be updated while iterating the SCD files or
// some update/insert/delete api coming.
class IIncSupportedIndex;
class IncSupportedIndexManager
{
public:
    ~IncSupportedIndexManager() {}
    void addIndex(boost::shared_ptr<IIncSupportedIndex> index);

    bool isRealTime();
    void flush();
    void optimize(bool wait);
    void preBuildFromSCD(size_t total_filesize);
    void postBuildFromSCD(time_t timestamp);
    void preMining();
    void postMining();
    void finishIndex();
    void finishRebuild();
    void preProcessForAPI();
    void postProcessForAPI();
    void setDocumentManager(boost::shared_ptr<DocumentManager>& documentManager);

    bool insertDocument(const Document& doc, time_t timestamp);
    bool updateDocument(const Document& olddoc, const Document& old_rtype_doc,
        const Document& newdoc, int updateType, time_t timestamp);
    void removeDocument(docid_t docid, time_t timestamp);

private:
    std::vector<boost::shared_ptr<IIncSupportedIndex> > inc_index_list_;
    boost::shared_ptr<DocumentManager> documentManager_;
};

}

#endif
