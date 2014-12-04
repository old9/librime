//
// Copyleft RIME Developers
// License: GPLv3
//
// Script translator
//
// 2011-07-10 GONG Chen <chen.sst@gmail.com>
//
#include <algorithm>
#include <boost/algorithm/string/join.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <rime/composition.h>
#include <rime/candidate.h>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/translation.h>
#include <rime/dict/dictionary.h>
#include <rime/algo/syllabifier.h>
#include <rime/gear/poet.h>
#include <rime/gear/script_translator.h>
#include <rime/gear/translator_commons.h>


//static const char* quote_left = "\xef\xbc\x88";
//static const char* quote_right = "\xef\xbc\x89";

namespace rime {

namespace {

struct DelimitSyllableState {
  const std::string* input;
  const std::string* delimiters;
  const SyllableGraph* graph;
  const Code* code;
  size_t end_pos;
  std::string output;
};

bool DelimitSyllablesDfs(DelimitSyllableState* state,
                         size_t current_pos, size_t depth) {
  if (depth == state->code->size()) {
    return current_pos == state->end_pos;
  }
  SyllableId syllable_id = state->code->at(depth);
  auto z = state->graph->edges.find(current_pos);
  if (z == state->graph->edges.end())
    return false;
  // favor longer spellings
  for (const auto& y : boost::adaptors::reverse(z->second)) {
    size_t end_vertex_pos = y.first;
    if (end_vertex_pos > state->end_pos)
      continue;
    auto x = y.second.find(syllable_id);
    if (x != y.second.end()) {
      size_t len = state->output.length();
      if (depth > 0 && len > 0 &&
          state->delimiters->find(
              state->output[len - 1]) == std::string::npos) {
        state->output += state->delimiters->at(0);
      }
      state->output += state->input->substr(current_pos,
                                            end_vertex_pos - current_pos);
      if (DelimitSyllablesDfs(state, end_vertex_pos, depth + 1))
        return true;
      state->output.resize(len);
    }
  }
  return false;
}

}  // anonymous namespace

class ScriptTranslation
    : public Translation,
      public Syllabification,
      public std::enable_shared_from_this<ScriptTranslation>
{
 public:
  ScriptTranslation(ScriptTranslator* translator,
                    const std::string& input, size_t start)
      : translator_(translator),
        input_(input), start_(start) {
    set_exhausted(true);
  }
  bool Evaluate(Dictionary* dict, UserDictionary* user_dict);
  virtual bool Next();
  virtual shared_ptr<Candidate> Peek();
  virtual size_t PreviousStop(size_t caret_pos) const;
  virtual size_t NextStop(size_t caret_pos) const;

 protected:
  bool CheckEmpty();
  bool IsNormalSpelling() const;
  template <class CandidateT>
  std::string GetPreeditString(const CandidateT& cand) const;
  template <class CandidateT>
  std::string GetOriginalSpelling(const CandidateT& cand) const;
  shared_ptr<Sentence> MakeSentence(Dictionary* dict,
                                    UserDictionary* user_dict);

  ScriptTranslator* translator_;
  std::string input_;
  size_t start_;

  SyllableGraph syllable_graph_;
  shared_ptr<DictEntryCollector> phrase_;
  shared_ptr<UserDictEntryCollector> user_phrase_;
  shared_ptr<Sentence> sentence_;

  DictEntryCollector::reverse_iterator phrase_iter_;
  UserDictEntryCollector::reverse_iterator user_phrase_iter_;
  size_t user_phrase_index_ = 0;
};

// ScriptTranslator implementation

ScriptTranslator::ScriptTranslator(const Ticket& ticket)
    : Translator(ticket),
      Memory(ticket),
      TranslatorOptions(ticket) {
  if (!engine_)
    return;
  if (Config* config = engine_->schema()->config()) {
    config->GetInt(name_space_ + "/spelling_hints", &spelling_hints_);
  }
}

shared_ptr<Translation> ScriptTranslator::Query(const std::string& input,
                                                const Segment& segment,
                                                std::string* prompt) {
  if (!dict_ || !dict_->loaded())
    return nullptr;
  if (!segment.HasTag(tag_))
    return nullptr;
  DLOG(INFO) << "input = '" << input
             << "', [" << segment.start << ", " << segment.end << ")";

  FinishSession();

  bool enable_user_dict = user_dict_ && user_dict_->loaded() &&
      !IsUserDictDisabledFor(input);

  // the translator should survive translations it creates
  auto result = New<ScriptTranslation>(this, input, segment.start);
  if (!result ||
      !result->Evaluate(dict_.get(),
                        enable_user_dict ? user_dict_.get() : NULL)) {
    return nullptr;
  }
  return New<UniqueFilter>(result);
}

std::string ScriptTranslator::FormatPreedit(const std::string& preedit) {
  std::string result = preedit;
  preedit_formatter_.Apply(&result);
  return result;
}

std::string ScriptTranslator::Spell(const Code& code) {
  std::string result;
  std::vector<std::string> syllables;
  if (!dict_ || !dict_->Decode(code, &syllables) || syllables.empty())
    return result;
  result =  boost::algorithm::join(syllables,
                                   std::string(1, delimiters_.at(0)));
  comment_formatter_.Apply(&result);
  return result;
}

bool ScriptTranslator::Memorize(const CommitEntry& commit_entry) {
  bool update_elements = false;
  // avoid updating single character entries within a phrase which is
  // composed with single characters only
  if (commit_entry.elements.size() > 1) {
    for (const DictEntry* e : commit_entry.elements) {
      if (e->code.size() > 1) {
        update_elements = true;
        break;
      }
    }
  }
  if (update_elements) {
    for (const DictEntry* e : commit_entry.elements) {
      user_dict_->UpdateEntry(*e, 0);
    }
  }
  user_dict_->UpdateEntry(commit_entry, 1);
  return true;
}

// ScriptTranslation implementation

bool ScriptTranslation::Evaluate(Dictionary* dict, UserDictionary* user_dict) {
  Syllabifier syllabifier(translator_->delimiters(),
                          translator_->enable_completion(),
                          translator_->strict_spelling());
  size_t consumed = syllabifier.BuildSyllableGraph(input_,
                                                   *dict->prism(),
                                                   &syllable_graph_);

  phrase_ = dict->Lookup(syllable_graph_, 0);
  if (user_dict) {
    user_phrase_ = user_dict->Lookup(syllable_graph_, 0);
  }
  if (!phrase_ && !user_phrase_)
    return false;
  // make sentences when there is no exact-matching phrase candidate
  size_t translated_len = 0;
  if (phrase_ && !phrase_->empty())
    translated_len = (std::max)(translated_len, phrase_->rbegin()->first);
  if (user_phrase_ && !user_phrase_->empty())
    translated_len = (std::max)(translated_len, user_phrase_->rbegin()->first);
  if (translated_len < consumed &&
      syllable_graph_.edges.size() > 1) {  // at least 2 syllables required
    sentence_ = MakeSentence(dict, user_dict);
  }

  if (phrase_)
    phrase_iter_ = phrase_->rbegin();
  if (user_phrase_)
    user_phrase_iter_ = user_phrase_->rbegin();
  return !CheckEmpty();
}

template <class CandidateT>
std::string
ScriptTranslation::GetPreeditString(const CandidateT& cand) const {
  DelimitSyllableState state;
  state.input = &input_;
  state.delimiters = &translator_->delimiters();
  state.graph = &syllable_graph_;
  state.code = &cand.code();
  state.end_pos = cand.end() - start_;
  if (bool success = DelimitSyllablesDfs(&state, cand.start() - start_, 0)) {
    return translator_->FormatPreedit(state.output);
  }
  else {
    return std::string();
  }
}

template <class CandidateT>
std::string
ScriptTranslation::GetOriginalSpelling(const CandidateT& cand) const {
  if (translator_ &&
      static_cast<int>(cand.code().size()) <= translator_->spelling_hints()) {
    return translator_->Spell(cand.code());
  }
  return std::string();
}

bool ScriptTranslation::Next() {
  if (exhausted())
    return false;
  if (sentence_) {
    sentence_.reset();
    return !CheckEmpty();
  }
  int user_phrase_code_length = 0;
  if (user_phrase_ && user_phrase_iter_ != user_phrase_->rend()) {
    user_phrase_code_length = user_phrase_iter_->first;
  }
  int phrase_code_length = 0;
  if (phrase_ && phrase_iter_ != phrase_->rend()) {
    phrase_code_length = phrase_iter_->first;
  }
  if (user_phrase_code_length > 0 &&
      user_phrase_code_length >= phrase_code_length) {
    DictEntryList& entries(user_phrase_iter_->second);
    if (++user_phrase_index_ >= entries.size()) {
      ++user_phrase_iter_;
      user_phrase_index_ = 0;
    }
  }
  else if (phrase_code_length > 0) {
    DictEntryIterator& iter(phrase_iter_->second);
    if (!iter.Next()) {
      ++phrase_iter_;
    }
  }
  return !CheckEmpty();
}

bool ScriptTranslation::IsNormalSpelling() const {
  return !syllable_graph_.vertices.empty() &&
      (syllable_graph_.vertices.rbegin()->second == kNormalSpelling);
}

shared_ptr<Candidate> ScriptTranslation::Peek() {
  if (exhausted())
    return nullptr;
  if (sentence_) {
    if (sentence_->preedit().empty()) {
      sentence_->set_preedit(GetPreeditString(*sentence_));
    }
    if (sentence_->comment().empty()) {
      std::string spelling(GetOriginalSpelling(*sentence_));
      if (!spelling.empty() &&
          spelling != sentence_->preedit()) {
        sentence_->set_comment(/*quote_left + */spelling/* + quote_right*/);
      }
    }
    return sentence_;
  }
  size_t user_phrase_code_length = 0;
  if (user_phrase_ && user_phrase_iter_ != user_phrase_->rend()) {
    user_phrase_code_length = user_phrase_iter_->first;
  }
  size_t phrase_code_length = 0;
  if (phrase_ && phrase_iter_ != phrase_->rend()) {
    phrase_code_length = phrase_iter_->first;
  }
  shared_ptr<Phrase> cand;
  if (user_phrase_code_length > 0 &&
      user_phrase_code_length >= phrase_code_length) {
    DictEntryList& entries(user_phrase_iter_->second);
    const auto& entry(entries[user_phrase_index_]);
    DLOG(INFO) << "user phrase '" << entry->text
               << "', code length: " << user_phrase_code_length;
    cand = New<Phrase>(translator_->language(),
                       "phrase",
                       start_,
                       start_ + user_phrase_code_length,
                       entry);
    cand->set_quality(entry->weight +
                      translator_->initial_quality() +
                      (IsNormalSpelling() ? 0.5 : -0.5));
  }
  else if (phrase_code_length > 0) {
    DictEntryIterator& iter(phrase_iter_->second);
    const auto& entry(iter.Peek());
    DLOG(INFO) << "phrase '" << entry->text
               << "', code length: " << user_phrase_code_length;
    cand = New<Phrase>(translator_->language(),
                       "phrase",
                       start_,
                       start_ + phrase_code_length,
                       entry);
    cand->set_quality(entry->weight +
                      translator_->initial_quality() +
                      (IsNormalSpelling() ? 0 : -1));
  }
  if (cand->preedit().empty()) {
    cand->set_preedit(GetPreeditString(*cand));
  }
  if (cand->comment().empty()) {
    std::string spelling = GetOriginalSpelling(*cand);
    if (!spelling.empty() && spelling != cand->preedit()) {
      cand->set_comment(/*quote_left + */spelling/* + quote_right*/);
    }
  }
  cand->set_syllabification(shared_from_this());
  return cand;
}

bool ScriptTranslation::CheckEmpty() {
  set_exhausted((!phrase_ || phrase_iter_ == phrase_->rend()) &&
                (!user_phrase_ || user_phrase_iter_ == user_phrase_->rend()));
  return exhausted();
}

shared_ptr<Sentence>
ScriptTranslation::MakeSentence(Dictionary* dict, UserDictionary* user_dict) {
  const int kMaxSyllablesForUserPhraseQuery = 5;
  const double kPenaltyForAmbiguousSyllable = 1e-10;
  WordGraph graph;
  for (const auto& x : syllable_graph_.edges) {
    // discourage starting a word from an ambiguous joint
    // bad cases include pinyin syllabification "niju'ede"
    double credibility = 1.0;
    if (syllable_graph_.vertices[x.first] >= kAmbiguousSpelling)
      credibility = kPenaltyForAmbiguousSyllable;
    UserDictEntryCollector& dest(graph[x.first]);
    if (user_dict) {
      auto user_phrase = user_dict->Lookup(syllable_graph_, x.first,
                                           kMaxSyllablesForUserPhraseQuery,
                                           credibility);
      if (user_phrase)
        dest.swap(*user_phrase);
    }
    if (auto phrase = dict->Lookup(syllable_graph_, x.first, credibility)) {
      // merge lookup results
      for (auto& y : *phrase) {
        DictEntryList& entries(dest[y.first]);
        if (entries.empty()) {
          entries.push_back(y.second.Peek());
        }
      }
    }
  }
  Poet poet(translator_->language());
  auto sentence = poet.MakeSentence(graph,
                                    syllable_graph_.interpreted_length);
  if (sentence) {
    sentence->Offset(start_);
    sentence->set_syllabification(shared_from_this());
  }
  return sentence;
}

size_t ScriptTranslation::PreviousStop(size_t caret_pos) const {
  size_t offset = caret_pos - start_;
  for (const auto& x : boost::adaptors::reverse(syllable_graph_.vertices)) {
    if (x.first < offset)
      return x.first + start_;
  }
  return caret_pos;
}

size_t ScriptTranslation::NextStop(size_t caret_pos) const {
  size_t offset = caret_pos - start_;
  for (const auto& x : syllable_graph_.vertices) {
    if (x.first > offset)
      return x.first + start_;
  }
  return caret_pos;
}

}  // namespace rime
