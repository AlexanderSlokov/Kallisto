#include "kallisto/btree_index.hpp"

namespace kallisto {

BTreeIndex::BTreeIndex(int degree) : min_degree_(degree) { root_node_ = std::make_unique<Node>(true); }

BTreeIndex::BTreeIndex(const BTreeIndex& other) : min_degree_(other.min_degree_) {
  if (other.root_node_) {
    root_node_ = std::make_unique<Node>(*other.root_node_);
  } else {
    root_node_ = std::make_unique<Node>(true);
  }
}

bool BTreeIndex::insertPath(const std::string& path) {
  if (containsPathRecursive(root_node_.get(), path)) {
    return true;
  }

  Node* root_ptr = root_node_.get();
  if (root_ptr->path_keys.size() == 2 * min_degree_ - 1) {
    auto new_root = std::make_unique<Node>(false);
    new_root->child_nodes.push_back(std::move(root_node_));
    root_node_ = std::move(new_root);
    splitChildNode(root_node_.get(), 0, root_node_->child_nodes[0].get());
    insertIntoNonFullNode(root_node_.get(), path);
  } else {
    insertIntoNonFullNode(root_ptr, path);
  }
  return true;
}

bool BTreeIndex::validatePath(const std::string& path) const { return containsPathRecursive(root_node_.get(), path); }

bool BTreeIndex::containsPathRecursive(Node* current_node, const std::string& path_key) const {
  int index = 0;
  while (index < current_node->path_keys.size() && path_key > current_node->path_keys[index]) {
    index++;
  }

  if (index < current_node->path_keys.size() && current_node->path_keys[index] == path_key) {
    return true;
  }

  if (current_node->is_leaf_node) {
    return false;
  }

  return containsPathRecursive(current_node->child_nodes[index].get(), path_key);
}

void BTreeIndex::insertIntoNonFullNode(Node* current_node, const std::string& path_key) {
  int index = current_node->path_keys.size() - 1;

  if (current_node->is_leaf_node) {
    current_node->path_keys.push_back("");
    while (index >= 0 && path_key < current_node->path_keys[index]) {
      current_node->path_keys[index + 1] = current_node->path_keys[index];
      index--;
    }
    current_node->path_keys[index + 1] = path_key;
  } else {
    while (index >= 0 && path_key < current_node->path_keys[index]) {
      index--;
    }
    index++;
    if (current_node->child_nodes[index]->path_keys.size() == 2 * min_degree_ - 1) {
      splitChildNode(current_node, index, current_node->child_nodes[index].get());
      if (path_key > current_node->path_keys[index]) {
        index++;
      }
    }
    insertIntoNonFullNode(current_node->child_nodes[index].get(), path_key);
  }
}

void BTreeIndex::splitChildNode(Node* parent_node, int child_index, Node* child_node) {
  auto new_sibling_node = std::make_unique<Node>(child_node->is_leaf_node);

  // Move mid_degree-1 keys to the new sibling node
  for (int j = 0; j < min_degree_ - 1; j++) {
    new_sibling_node->path_keys.push_back(child_node->path_keys[j + min_degree_]);
  }

  if (!child_node->is_leaf_node) {
    for (int j = 0; j < min_degree_; j++) {
      new_sibling_node->child_nodes.push_back(std::move(child_node->child_nodes[j + min_degree_]));
    }
    child_node->child_nodes.erase(child_node->child_nodes.begin() + min_degree_, child_node->child_nodes.end());
  }

  std::string middle_key = child_node->path_keys[min_degree_ - 1];
  child_node->path_keys.erase(child_node->path_keys.begin() + min_degree_ - 1, child_node->path_keys.end());

  parent_node->child_nodes.insert(parent_node->child_nodes.begin() + child_index + 1, std::move(new_sibling_node));
  parent_node->path_keys.insert(parent_node->path_keys.begin() + child_index, middle_key);
}

} // namespace kallisto
