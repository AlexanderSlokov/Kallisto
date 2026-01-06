#include "kallisto/btree_index.hpp"

namespace kallisto {

BTreeIndex::BTreeIndex(int degree) : min_degree(degree) {
    root = std::make_unique<Node>(true);
}

bool BTreeIndex::insert_path(const std::string& path) {
    if (validate_path(path)) return true;

    Node* r = root.get();
    if (r->keys.size() == 2 * min_degree - 1) {
        auto s = std::make_unique<Node>(false);
        s->children.push_back(std::move(root));
        root = std::move(s);
        split_child(root.get(), 0, root->children[0].get());
        insert_non_full(root.get(), path);
    } else {
        insert_non_full(r, path);
    }
    return true;
}

bool BTreeIndex::validate_path(const std::string& path) const {
    return search(root.get(), path);
}

bool BTreeIndex::search(Node* node, const std::string& key) const {
    int i = 0;
    while (i < node->keys.size() && key > node->keys[i]) {
        i++;
    }

    if (i < node->keys.size() && node->keys[i] == key) {
        return true;
    }

    if (node->is_leaf) {
        return false;
    }

    return search(node->children[i].get(), key);
}

void BTreeIndex::insert_non_full(Node* node, const std::string& key) {
    int i = node->keys.size() - 1;

    if (node->is_leaf) {
        node->keys.push_back("");
        while (i >= 0 && key < node->keys[i]) {
            node->keys[i + 1] = node->keys[i];
            i--;
        }
        node->keys[i + 1] = key;
    } else {
        while (i >= 0 && key < node->keys[i]) {
            i--;
        }
        i++;
        if (node->children[i]->keys.size() == 2 * min_degree - 1) {
            split_child(node, i, node->children[i].get());
            if (key > node->keys[i]) {
                i++;
            }
        }
        insert_non_full(node->children[i].get(), key);
    }
}

void BTreeIndex::split_child(Node* parent, int i, Node* child) {
    auto z = std::make_unique<Node>(child->is_leaf);
    
    // Move mid_degree-1 keys to z
    for (int j = 0; j < min_degree - 1; j++) {
        z->keys.push_back(child->keys[j + min_degree]);
    }

    if (!child->is_leaf) {
        for (int j = 0; j < min_degree; j++) {
            z->children.push_back(std::move(child->children[j + min_degree]));
        }
        child->children.erase(child->children.begin() + min_degree, child->children.end());
    }

    std::string mid_key = child->keys[min_degree - 1];
    child->keys.erase(child->keys.begin() + min_degree - 1, child->keys.end());

    parent->children.insert(parent->children.begin() + i + 1, std::move(z));
    parent->keys.insert(parent->keys.begin() + i, mid_key);
}

} // namespace kallisto
