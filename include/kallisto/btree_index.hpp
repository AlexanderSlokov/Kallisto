#pragma once

#include <string>
#include <vector>
#include <memory>
#include <algorithm>

namespace kallisto {

/**
 * This is a simplified B-Tree optimized for strings (paths).
 * It acts as a validator before secret lookup in the CuckooTable.
 */
class BTreeIndex {
public:
    /**
     * @param degree Minimum degree (t). A node can have at most 2t-1 keys.
     */
    BTreeIndex(int degree = 3);

    /**
     * Inserts a path into the index.
     * @param path The path to insert (e.g., "/prod/db").
     * @return true if insertion was successful.
     */
    bool insert_path(const std::string& path);

    /**
     * Validates if a path exists in the index.
     * @param path The path to validate.
     * @return true if the path exists.
     */
    bool validate_path(const std::string& path) const;

private:
    struct Node {
        bool is_leaf;
        std::vector<std::string> keys;
        std::vector<std::unique_ptr<Node>> children;

        Node(bool leaf = true) : is_leaf(leaf) {}
    };

    std::unique_ptr<Node> root;
    int min_degree; // Fixed the cryptic "t"

    void split_child(Node* parent, int i, Node* child);
    void insert_non_full(Node* node, const std::string& key);
    bool search(Node* node, const std::string& key) const;
};

} // namespace kallisto
