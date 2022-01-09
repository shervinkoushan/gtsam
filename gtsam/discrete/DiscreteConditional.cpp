/* ----------------------------------------------------------------------------

 * GTSAM Copyright 2010, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list)

 * See LICENSE for the license information

 * -------------------------------------------------------------------------- */

/**
 *  @file DiscreteConditional.cpp
 *  @date Feb 14, 2011
 *  @author Duy-Nguyen Ta
 *  @author Frank Dellaert
 */

#include <gtsam/discrete/DiscreteConditional.h>
#include <gtsam/discrete/Signature.h>
#include <gtsam/inference/Conditional-inst.h>
#include <gtsam/base/Testable.h>
#include <gtsam/base/debug.h>

#include <boost/make_shared.hpp>

#include <algorithm>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

using namespace std;
using std::stringstream;
using std::vector;
using std::pair;
namespace gtsam {

// Instantiate base class
template class GTSAM_EXPORT Conditional<DecisionTreeFactor, DiscreteConditional> ;

/* ******************************************************************************** */
DiscreteConditional::DiscreteConditional(const size_t nrFrontals,
    const DecisionTreeFactor& f) :
    BaseFactor(f / (*f.sum(nrFrontals))), BaseConditional(nrFrontals) {
}

/* ******************************************************************************** */
DiscreteConditional::DiscreteConditional(const DecisionTreeFactor& joint,
    const DecisionTreeFactor& marginal) :
    BaseFactor(
        ISDEBUG("DiscreteConditional::COUNT") ? joint : joint / marginal), BaseConditional(
            joint.size()-marginal.size()) {
  if (ISDEBUG("DiscreteConditional::DiscreteConditional"))
    cout << (firstFrontalKey()) << endl; //TODO Print all keys
}

/* ******************************************************************************** */
DiscreteConditional::DiscreteConditional(const DecisionTreeFactor& joint,
    const DecisionTreeFactor& marginal, const Ordering& orderedKeys) :
    DiscreteConditional(joint, marginal) {
  keys_.clear();
  keys_.insert(keys_.end(), orderedKeys.begin(), orderedKeys.end());
}

/* ******************************************************************************** */
DiscreteConditional::DiscreteConditional(const Signature& signature)
    : BaseFactor(signature.discreteKeys(), signature.cpt()),
      BaseConditional(1) {}

/* ******************************************************************************** */
void DiscreteConditional::print(const string& s,
                                const KeyFormatter& formatter) const {
  cout << s << " P( ";
  for (const_iterator it = beginFrontals(); it != endFrontals(); ++it) {
    cout << formatter(*it) << " ";
  }
  if (nrParents()) {
    cout << "| ";
    for (const_iterator it = beginParents(); it != endParents(); ++it) {
      cout << formatter(*it) << " ";
    }
  }
  cout << ")";
  ADT::print("");
  cout << endl;
}

/* ******************************************************************************** */
bool DiscreteConditional::equals(const DiscreteFactor& other,
    double tol) const {
  if (!dynamic_cast<const DecisionTreeFactor*>(&other))
    return false;
  else {
    const DecisionTreeFactor& f(
        static_cast<const DecisionTreeFactor&>(other));
    return DecisionTreeFactor::equals(f, tol);
  }
}

/* ******************************************************************************** */
static DiscreteConditional::ADT Choose(const DiscreteConditional& conditional,
                                       const DiscreteValues& parentsValues) {
  // Get the big decision tree with all the levels, and then go down the
  // branches based on the value of the parent variables.
  DiscreteConditional::ADT adt(conditional);
  size_t value;
  for (Key j : conditional.parents()) {
    try {
      value = parentsValues.at(j);
      adt = adt.choose(j, value);  // ADT keeps getting smaller.
    } catch (std::out_of_range&) {
      parentsValues.print("parentsValues: ");
      throw runtime_error("DiscreteConditional::choose: parent value missing");
    };
  }
  return adt;
}

/* ******************************************************************************** */
DecisionTreeFactor::shared_ptr DiscreteConditional::choose(
    const DiscreteValues& parentsValues) const {
  // Get the big decision tree with all the levels, and then go down the
  // branches based on the value of the parent variables.
  ADT adt(*this);
  size_t value;
  for (Key j : parents()) {
    try {
      value = parentsValues.at(j);
      adt = adt.choose(j, value);  // ADT keeps getting smaller.
    } catch (exception&) {
      parentsValues.print("parentsValues: ");
      throw runtime_error("DiscreteConditional::choose: parent value missing");
    };
  }

  // Convert ADT to factor.
  DiscreteKeys discreteKeys;
  for (Key j : frontals()) {
    discreteKeys.emplace_back(j, this->cardinality(j));
  }
  return boost::make_shared<DecisionTreeFactor>(discreteKeys, adt);
}

/* ******************************************************************************** */
DecisionTreeFactor::shared_ptr DiscreteConditional::likelihood(
    const DiscreteValues& frontalValues) const {
  // Get the big decision tree with all the levels, and then go down the
  // branches based on the value of the frontal variables.
  ADT adt(*this);
  size_t value;
  for (Key j : frontals()) {
    try {
      value = frontalValues.at(j);
      adt = adt.choose(j, value);  // ADT keeps getting smaller.
    } catch (exception&) {
      frontalValues.print("frontalValues: ");
      throw runtime_error("DiscreteConditional::choose: frontal value missing");
    };
  }

  // Convert ADT to factor.
  DiscreteKeys discreteKeys;
  for (Key j : parents()) {
    discreteKeys.emplace_back(j, this->cardinality(j));
  }
  return boost::make_shared<DecisionTreeFactor>(discreteKeys, adt);
}

/* ******************************************************************************** */
DecisionTreeFactor::shared_ptr DiscreteConditional::likelihood(
    size_t parent_value) const {
  if (nrFrontals() != 1)
    throw std::invalid_argument(
        "Single value likelihood can only be invoked on single-variable "
        "conditional");
  DiscreteValues values;
  values.emplace(keys_[0], parent_value);
  return likelihood(values);
}

/* ******************************************************************************** */
void DiscreteConditional::solveInPlace(DiscreteValues* values) const {
  // TODO: Abhijit asks: is this really the fastest way? He thinks it is.
  ADT pFS = Choose(*this, *values); // P(F|S=parentsValues)

  // Initialize
  DiscreteValues mpe;
  double maxP = 0;

  DiscreteKeys keys;
  for(Key idx: frontals()) {
    DiscreteKey dk(idx, cardinality(idx));
    keys & dk;
  }
  // Get all Possible Configurations
  const auto allPosbValues = cartesianProduct(keys);

  // Find the MPE
  for(const auto& frontalVals: allPosbValues) {
    double pValueS = pFS(frontalVals); // P(F=value|S=parentsValues)
    // Update MPE solution if better
    if (pValueS > maxP) {
      maxP = pValueS;
      mpe = frontalVals;
    }
  }

  //set values (inPlace) to mpe
  for(Key j: frontals()) {
    (*values)[j] = mpe[j];
  }
}

/* ******************************************************************************** */
void DiscreteConditional::sampleInPlace(DiscreteValues* values) const {
  assert(nrFrontals() == 1);
  Key j = (firstFrontalKey());
  size_t sampled = sample(*values); // Sample variable given parents
  (*values)[j] = sampled; // store result in partial solution
}

/* ******************************************************************************** */
size_t DiscreteConditional::solve(const DiscreteValues& parentsValues) const {

  // TODO: is this really the fastest way? I think it is.
  ADT pFS = Choose(*this, parentsValues); // P(F|S=parentsValues)

  // Then, find the max over all remaining
  // TODO, only works for one key now, seems horribly slow this way
  size_t mpe = 0;
  DiscreteValues frontals;
  double maxP = 0;
  assert(nrFrontals() == 1);
  Key j = (firstFrontalKey());
  for (size_t value = 0; value < cardinality(j); value++) {
    frontals[j] = value;
    double pValueS = pFS(frontals); // P(F=value|S=parentsValues)
    // Update MPE solution if better
    if (pValueS > maxP) {
      maxP = pValueS;
      mpe = value;
    }
  }
  return mpe;
}

/* ******************************************************************************** */
size_t DiscreteConditional::sample(const DiscreteValues& parentsValues) const {
  static mt19937 rng(2);  // random number generator

  // Get the correct conditional density
  ADT pFS = Choose(*this, parentsValues);  // P(F|S=parentsValues)

  // TODO(Duy): only works for one key now, seems horribly slow this way
  if (nrFrontals() != 1) {
    throw std::invalid_argument(
        "DiscreteConditional::sample can only be called on single variable "
        "conditionals");
  }
  Key key = firstFrontalKey();
  size_t nj = cardinality(key);
  vector<double> p(nj);
  DiscreteValues frontals;
  for (size_t value = 0; value < nj; value++) {
    frontals[key] = value;
    p[value] = pFS(frontals);  // P(F=value|S=parentsValues)
    if (p[value] == 1.0) {
      return value;  // shortcut exit
    }
  }
  std::discrete_distribution<size_t> distribution(p.begin(), p.end());
  return distribution(rng);
}

/* ******************************************************************************** */
size_t DiscreteConditional::sample(size_t parent_value) const {
  if (nrParents() != 1)
    throw std::invalid_argument(
        "Single value sample() can only be invoked on single-parent "
        "conditional");
  DiscreteValues values;
  values.emplace(keys_.back(), parent_value);
  return sample(values);
}

/* ******************************************************************************** */
size_t DiscreteConditional::sample() const {
  if (nrParents() != 0)
    throw std::invalid_argument(
        "sample() can only be invoked on no-parent prior");
  DiscreteValues values;
  return sample(values);
}

/* ************************************************************************* */
vector<Assignment<Key>> DiscreteConditional::frontalAssignments() const {
  vector<pair<Key, size_t>> pairs;
  for (Key key : frontals()) pairs.emplace_back(key, cardinalities_.at(key));
  vector<pair<Key, size_t>> rpairs(pairs.rbegin(), pairs.rend());
  return cartesianProduct(rpairs);
}

/* ************************************************************************* */
vector<Assignment<Key>> DiscreteConditional::allAssignments() const {
  vector<pair<Key, size_t>> pairs;
  for (Key key : parents()) pairs.emplace_back(key, cardinalities_.at(key));
  for (Key key : frontals()) pairs.emplace_back(key, cardinalities_.at(key));
  vector<pair<Key, size_t>> rpairs(pairs.rbegin(), pairs.rend());
  return cartesianProduct(rpairs);
}

/* ************************************************************************* */
// Print out signature.
static void streamSignature(const DiscreteConditional& conditional,
                            const KeyFormatter& keyFormatter,
                            stringstream* ss) {
  *ss << "P(";
  bool first = true;
  for (Key key : conditional.frontals()) {
    if (!first) *ss << ",";
    *ss << keyFormatter(key);
    first = false;
  }
  if (conditional.nrParents() > 0) {
    *ss << "|";
    bool first = true;
    for (Key parent : conditional.parents()) {
      if (!first) *ss << ",";
      *ss << keyFormatter(parent);
      first = false;
    }
  }
  *ss << "):";
}

/* ************************************************************************* */
std::string DiscreteConditional::markdown(const KeyFormatter& keyFormatter,
                                          const Names& names) const {
  stringstream ss;
  ss << " *";
  streamSignature(*this, keyFormatter, &ss);
  ss << "*\n" << std::endl;
  if (nrParents() == 0) {
    // We have no parents, call factor method.
    ss << DecisionTreeFactor::markdown(keyFormatter, names);
    return ss.str();
  }

  // Print out header and construct argument for `cartesianProduct`.
  ss << "|";
  for (Key parent : parents()) {
    ss << "*" << keyFormatter(parent) << "*|";
  }

  size_t n = 1;
  for (Key key : frontals()) {
    size_t k = cardinalities_.at(key);
    n *= k;
  }
  for (const auto& a : frontalAssignments()) {
    for (auto&& it = beginFrontals(); it != endFrontals(); ++it) {
      size_t index = a.at(*it);
      ss << Translate(names, *it, index);
    }
    ss << "|";
  }
  ss << "\n";

  // Print out separator with alignment hints.
  ss << "|";
  for (size_t j = 0; j < nrParents() + n; j++) ss << ":-:|";
  ss << "\n";

  // Print out all rows.
  size_t count = 0;
  for (const auto& a : allAssignments()) {
    if (count == 0) {
      ss << "|";
      for (auto&& it = beginParents(); it != endParents(); ++it) {
        size_t index = a.at(*it);
        ss << Translate(names, *it, index) << "|";
      }
    }
    ss << operator()(a) << "|";
    count = (count + 1) % n;
    if (count == 0) ss << "\n";
  }
  return ss.str();
}

/* ************************************************************************ */
string DiscreteConditional::html(const KeyFormatter& keyFormatter,
                                 const Names& names) const {
  stringstream ss;
  ss << "<div>\n<p>  <i>";
  streamSignature(*this, keyFormatter, &ss);
  ss << "</i></p>\n";
  if (nrParents() == 0) {
    // We have no parents, call factor method.
    ss << DecisionTreeFactor::html(keyFormatter, names);
    return ss.str();
  }

  // Print out preamble.
  ss << "<table class=\'DiscreteConditional\'>\n  <thead>\n";

  // Print out header row.
  ss << "    <tr>";
  for (auto& key : keys()) {
    ss << "<th>" << keyFormatter(key) << "</th>";
  }
  ss << "<th>value</th></tr>\n";

  // Finish header and start body.
  ss << "  </thead>\n  <tbody>\n";

  // Print out all rows.
  auto rows = enumerate();
  for (const auto& kv : rows) {
    ss << "    <tr>";
    auto assignment = kv.first;
    for (auto& key : keys()) {
      size_t index = assignment.at(key);
      ss << "<th>" << Translate(names, key, index) << "</th>";
    }
    ss << "<td>" << kv.second << "</td>";  // value
    ss << "</tr>\n";
  }
  ss << "  </tbody>\n</table>\n</div>";

  return ss.str();
}

/* ************************************************************************* */

}  // namespace gtsam
