#include "database.h"
#include <ctime>
#include <fstream>

Database::Database(string ticker) {
  string uri = getenv("MONGO_DB_URI");
  string database = getenv("MONGO_DB_DATABASE");

  client_ = mongocxx::client{
    mongocxx::uri{uri}
  };
  database_ = client_[database];
  // ifstream tickerFile("tickers");
  // string temp;
  // while (getline(tickerFile, temp)) {
    // if (temp != "") {
  // sma_bars = Database::Queue{50};
  update_bars(ticker);
      // sma_bars[temp] = Database::Queue{50};
      // update_bars(temp);
    // }
  // }
}

mongocxx::cursor Database::query_database(string collection_name, vector<QueryBase*> query)
{
  vector<bsoncxx::document::view> documents;
  mongocxx::collection collection = database_[collection_name];
  bsoncxx::builder::basic::document doc = document{};
  for (unsigned int i = 0; i < query.size(); i++)
  {
    QueryBase* _query = query[i];
    unsigned int type = _query->query_type;
    switch (type) {
      case 0:
        parse_query<unsigned short>(_query, &doc);
        break;
      case 1:
        parse_query<int>(_query, &doc);
        break;
      case 2:
        parse_query<double>(_query, &doc);
        break;
    }
  }

  mongocxx::cursor cursor = collection.find(
      doc.extract()
  );

  return cursor;
}

void Database::update_bars(string ticker)
{
  if (sma_bars.size < sma_bars.max_size)
  {
    Time timeObj;
    timeObj--;
    Bar* temp = get_bar(ticker, timeObj._time[0], timeObj._time[1]);
    unsigned short counter = sma_bars.size;
    while (temp != NULL && counter < sma_bars.max_size)
    {
      sma_bars.enqueueHead(temp);
      timeObj--;
      temp = get_bar(ticker, timeObj._time[0], timeObj._time[1]);
      counter++;
    }
  }
  else
  {
    Time timeObj;
    if (timeObj._time[0] == sma_bars.last_hour && timeObj._time[1] == sma_bars.last_min) {
      Queue::Node* temp = (sma_bars.tail)->prev;
      Queue::Node* newTail = new Queue::Node(temp, get_bar(ticker, sma_bars.last_hour, sma_bars.last_min));
      delete sma_bars.tail;
      temp->next = newTail;
      sma_bars.tail = newTail;
    }
    else
    {
        Bar* remove = sma_bars.dequeue();
        delete remove;
        Bar* _new = get_bar(ticker, timeObj._time[0], timeObj._time[1]);
        if (_new == NULL) return;
        sma_bars.enqueue(_new);
        sma_bars.last_hour = timeObj._time[0];
        sma_bars.last_min = timeObj._time[1];
    }
  }
}

Bar* Database::get_bar(string ticker, unsigned short hour, unsigned short minute) {
  vector<QueryBase*> query;
  Query<unsigned short>* hour_query = new Query<unsigned short>("HOUR", hour);
  Query<unsigned short>* minute_query = new Query<unsigned short>("MINUTE", minute);
  query.push_back(hour_query);
  query.push_back(minute_query);
  mongocxx::cursor result = query_database(ticker, query);

  double min = numeric_limits<double>::max();
  double max = numeric_limits<double>::min();
  double open, close;
  double temp = 0;
  bool first = 1;
  for (mongocxx::cursor::iterator iter = result.begin(); iter != result.end(); iter++) {
    double last_price = 0;
    try {
      last_price = (*iter)["LAST_PRICE"].get_double().value;
    }
    catch (...) {
      last_price = (double) (*iter)["LAST_PRICE"].get_int32().value;
    }
    if (first) {
      first = !first;
      open = last_price;
    }
    if (last_price < min) min = last_price;
    if (last_price > max) max = last_price;
    temp = last_price;
  }
  close = temp;
  if (!(min == numeric_limits<double>::max() || max == numeric_limits<double>::min()))
    return new Bar(ticker, hour, minute, open, close, min, max);
  else return NULL;
}

vector<Bar*> Database::get_bars(string ticker, unsigned short hour_start, unsigned short hour_end, unsigned short minute_start, unsigned short minute_end)
{
  vector<Bar*> bars;
  unsigned short hour = hour_start;
  for (unsigned short i = minute_start; i <= ((hour_end - hour_start) * 60) + minute_end; i++) {
    if (i != minute_start && i % 60 == 0) hour++;
    Bar* bar = get_bar(ticker, hour, i % 60);
    if (bar != NULL) bars.push_back(bar);
  }
  return bars;
}

double Database::get_sma(string ticker, unsigned short offset)
{
  double sum = 0;
  update_bars(ticker);
  if (offset > sma_bars.max_size || offset > sma_bars.size) return 0;
  Database::Queue::iterator iter = sma_bars.begin_from_end();
  for (unsigned short i = 0; i < offset; i++) {
    Bar* bar = iter.value();
    if (bar == NULL) {
      return 0;
    }
    sum += bar->close;
    iter--;
  }
  return sum / offset;
}

double Database::get_ema(string ticker, unsigned short offset)
{
  if (offset > sma_bars.max_size || offset > sma_bars.size) return 0;
  return get_ema(ticker, offset, offset, sma_bars.begin_from_end());
}

double Database::get_ema(string ticker, unsigned short offset, unsigned short adjOffset, Database::Queue::iterator iter)
{
  double price = iter.value()->close;
  iter--;
  return price * (2 / (offset + 1)) + get_ema(ticker, offset, --adjOffset, iter);
}

Time::Time() {
  time_t now = time(0);
  tm* ltm = localtime(&now);
  _time[0] = ltm->tm_hour;
  _time[1] = ltm->tm_min;
  _time[2] = ltm->tm_sec;
}

Time::Time(unsigned short hour, unsigned short minute) {
  _time[0] = hour;
  _time[1] = minute;
  _time[2] = 0;
}

Time::Time(unsigned short hour, unsigned short minute, unsigned short second) {
  _time[0] = hour;
  _time[1] = minute;
  _time[2] = second;
}

void Time::operator++(int value) {
  if (_time[1] == 59) {
    _time[0] = _time[0] + 1;
    _time[1] = 0;
  }
  else _time[1] = _time[1] + 1;
}

void Time::operator--(int value) {
  if (_time[1] == 0) {
    _time[0] = _time[0] - 1;
    _time[1] = 59;
  }
  else _time[1] = _time[1] - 1;
}

Bar::Bar(string ticker_, unsigned short hour_, unsigned short minute_, double open_, double close_, double low_, double high_) {
  ticker = ticker_;
  hour = hour_;
  minute = minute_;
  open = open_;
  close = close_;
  low = low_;
  high = high_;
}

Database::Queue::Queue(unsigned short _max_size) {
  max_size = _max_size;
}

Bar* Database::Queue::dequeue() {
  Node* temp = head;
  if (head->next == NULL) {
    head = NULL;
    return temp->value;
  }
  head->next->prev = NULL;
  head = head->next;
  size--;
  return temp->value;
}

void Database::Queue::enqueueHead(Bar* _bar) {
  size++;
  if (head == NULL && tail == NULL) {
    head = new Node(_bar);
    tail = head;
    return;
  }
  Node* temp = new Node(_bar, head);
  head->prev = temp;
  temp->next = head;
  head = temp;
}

void Database::Queue::enqueue(Bar* _bar) {
  size++;
  if (head == NULL && tail == NULL) {
    head = new Node(_bar);
    tail = head;
    return;
  }
  Node* temp = new Node(tail, _bar);
  tail->next = temp;
  temp->prev = tail;
  tail = temp;
}

Bar* Database::Queue::peek() {
  return head->value;
}

bool Database::Queue::isEmpty() {
  return size == 0;
}

bool Database::Queue::isFull() {
  return size == max_size;
}

Database::Queue::iterator Database::Queue::begin() {
  return Database::Queue::iterator(head);
}

Database::Queue::iterator Database::Queue::begin_from_end() {
  return Database::Queue::iterator(tail);
}

Database::Queue::iterator Database::Queue::end() {
  return Database::Queue::iterator(NULL);
}

Bar* Database::Queue::iterator::value() {
  if (node == NULL) return NULL;
  return node->value;
}

void Database::Queue::iterator::operator++(int value) {
  node = node->next;
}

void Database::Queue::iterator::operator--(int value) {
  node = node->prev;
}

bool Database::Queue::iterator::operator==(iterator second) {
  return value() == second.value();
}

bool Database::Queue::iterator::operator!=(iterator second) {
  return value() != second.value();
}

bool Time::operator==(Time second) {
  return _time[0] == second._time[0] && _time[1] == second._time[1];
}

bool Time::operator!=(Time second) {
  return !(*this == second);
}
