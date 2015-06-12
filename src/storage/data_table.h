/*-------------------------------------------------------------------------
 *
 * data_table.h
 * file description
 *
 * Copyright(c) 2015, CMU
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "catalog/manager.h"
#include "storage/abstract_table.h"
#include "storage/tile_group.h"
#include "storage/backend_vm.h"

#include "index/index.h"

#include <string>

namespace nstore {
namespace storage {

//===--------------------------------------------------------------------===//
// DataTable
//===--------------------------------------------------------------------===//

/**
 * Represents a group of tile groups logically vertically contiguous.
 *
 * <Tile Group 1>
 * <Tile Group 2>
 * ...
 * <Tile Group n>
 *
 */
class DataTable {
    friend class TileGroup;
    friend class TableFactory;

    DataTable() = delete;
    DataTable(DataTable const&) = delete;

public:

    // Table constructor
    DataTable(catalog::Schema *schema,
              Backend *backend,
              std::string table_name);

    ~DataTable();

    std::string GetName() const {
        return table_name;
    }

    //===--------------------------------------------------------------------===//
    // INDEXES
    //===--------------------------------------------------------------------===//

    // add an index to the table
    void AddIndex(index::Index *index);
    
    inline size_t GetIndexCount() const {
        return indexes.size();
    }

    inline index::Index *GetIndex(oid_t index_id) const {
        assert(index_id < indexes.size());
        return indexes[index_id];
    }

    void InsertInIndexes(const storage::Tuple *tuple, ItemPointer location);
    bool TryInsertInIndexes(const storage::Tuple *tuple, ItemPointer location);

    void DeleteInIndexes(const storage::Tuple *tuple);

    bool CheckNulls(const storage::Tuple *tuple) const;

    //===--------------------------------------------------------------------===//
    // UTILITIES
    //===--------------------------------------------------------------------===//

    // Get a string representation of this table
    friend std::ostream& operator<<(std::ostream& os, const DataTable& table);

protected:

    //===--------------------------------------------------------------------===//
    // MEMBERS
    //===--------------------------------------------------------------------===//

    // INDEXES
    std::vector<index::Index*> indexes;

};


} // End storage namespace
} // End nstore namespace


