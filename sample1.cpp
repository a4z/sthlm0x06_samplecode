#include <memory>
#include <cstdlib>
#include <iostream>
#include <sqlite3.h>

  template< bool B, class T = void >
  using enable_if_t = typename std::enable_if<B,T>::type;

//
// not_null
// borrowed from GLS,  https://github.com/Microsoft/GSL
// backported to copmile with gcc 4.8 on RHEL 6
//
// Restricts a pointer or smart pointer to only hold non-null values.
//
// Has zero size overhead over T.
//
// If T is a pointer (i.e. T == U*) then
// - allow construction from U* or U&
// - disallow construction from nullptr_t
// - disallow default construction
// - ensure construction from U* fails with nullptr
// - allow implicit conversion to U*
//
template <class T>
class not_null
{
    static_assert(std::is_assignable<T&, std::nullptr_t>::value, "T cannot be assigned nullptr.");

public:
    not_null(T t) : ptr_(t) { ensure_invariant(); }
    not_null& operator=(const T& t)
    {
        ptr_ = t;
        ensure_invariant();
        return *this;
    }

    not_null(const not_null& other) = default;
    not_null& operator=(const not_null& other) = default;

    template <typename U, typename Dummy = enable_if_t<std::is_convertible<U, T>::value>>
    not_null(const not_null<U>& other)
    {
        *this = other;
    }

    template <typename U, typename Dummy = enable_if_t<std::is_convertible<U, T>::value>>
    not_null& operator=(const not_null<U>& other)
    {
        ptr_ = other.get();
        return *this;
    }

    // prevents compilation when someone attempts to assign a nullptr
    not_null(std::nullptr_t) = delete;
    not_null(int) = delete;
    not_null<T>& operator=(std::nullptr_t) = delete;
    not_null<T>& operator=(int) = delete;

    T get() const
    {
#ifdef _MSC_VER
        __assume(ptr_ != nullptr);
#endif
        return ptr_;
    } // the assume() should help the optimizer

    operator T() const { return get(); }
    T operator->() const { return get(); }

    bool operator==(const T& rhs) const { return ptr_ == rhs; }
    bool operator!=(const T& rhs) const { return !(*this == rhs); }
private:
    T ptr_;

    // we assume that the compiler can hoist/prove away most of the checks inlined from this
    // function
    // if not, we could make them optional via conditional compilation
    void ensure_invariant() const { Ensure(ptr_ != nullptr); }

    // tmpfix, until inlcude the defaultu assing
    void Ensure(bool flag) const { if(not flag) throw "Ensure failed" ;}

    // unwanted operators...pointers only point to single objects!
    // TODO ensure all arithmetic ops on this type are unavailable
    not_null<T>& operator++() = delete;
    not_null<T>& operator--() = delete;
    not_null<T> operator++(int) = delete;
    not_null<T> operator--(int) = delete;
    not_null<T>& operator+(size_t) = delete;
    not_null<T>& operator+=(size_t) = delete;
    not_null<T>& operator-(size_t) = delete;
    not_null<T>& operator-=(size_t) = delete;
};

using database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)> ;

database open_database(const char* name)
{
  sqlite3* db = nullptr;
  auto rc = sqlite3_open (name, &db);
  if(rc != SQLITE_OK) {
    std::cerr << "Unable to open database '" << name << "': "
              <<  sqlite3_errmsg (db);
    sqlite3_close (db);
    std::exit(EXIT_FAILURE);
  }
  return database{db, sqlite3_close} ;
}

void execute (not_null<sqlite3*> db, const char* sql)
{
  char* errmsg = 0;
  int rc = sqlite3_exec (db, sql, 0, 0, &errmsg);
  if (rc != SQLITE_OK) {
    std::cerr << "Unable to execute '" << sql << "': "
              <<  errmsg ;
    sqlite3_free(errmsg) ;
    std::exit(EXIT_FAILURE);
  }
}


using statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> ;

statement create_statement(not_null<sqlite3*> db, const std::string& sql)
{
  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2 (db,
                              sql.c_str (), sql.length(),
                              &stmt, nullptr);
  if (rc != SQLITE_OK) {
    std::cerr << "Unable to create statement '" << sql << "': "
              <<  sqlite3_errmsg(db);
    std::exit(EXIT_FAILURE);
  }
  return statement(stmt, sqlite3_finalize);
}


using stmt_callback =
    std::function<bool(not_null<sqlite3_stmt*>)> ;

void run(not_null<sqlite3_stmt*> stmt,
        stmt_callback callback = stmt_callback{})
{
  using reset_guard
      = std::unique_ptr<sqlite3_stmt, decltype (&sqlite3_reset)>;

  auto reset = reset_guard (stmt.get(), &sqlite3_reset);

  auto step_next = [&](int rc){
    if (rc == SQLITE_OK || rc == SQLITE_DONE)
      return false ;
    else if (rc == SQLITE_ROW)
      if(callback)
        return callback(stmt);
    // else ... some error handling
    return false ;
  };

  while(step_next(sqlite3_step(stmt))) ;
}




bool dump_current_row(not_null<sqlite3_stmt*> stmt)
{
  for (int i = 0 ; i < sqlite3_column_count(stmt); ++i) {
    auto columntype = sqlite3_column_type(stmt, i) ;

    if(columntype == SQLITE_NULL) {
      std::cout << "<NULL>" ;
    }
    else if (columntype == SQLITE_INTEGER){
      std::cout << sqlite3_column_int64(stmt, i);
    }
    else if (columntype == SQLITE_FLOAT){
      std::cout << sqlite3_column_double(stmt, i) ;
    }
    else if (columntype == SQLITE_TEXT ){
      auto first = sqlite3_column_text (stmt, i);
      std::size_t s = sqlite3_column_bytes (stmt, i);
      std::cout << "'" << (s > 0 ?
          std::string((const char*)first, s)  : "") << "'";
    }
    else if (columntype == SQLITE_BLOB ){
      std::cout << "<BLO000B>" ;
    }
    std::cout << "|" ;
  }
  std::cout << "\n" ;
  return true ;
}


bool print_thing(not_null<sqlite3_stmt*> stmt) {

  auto id = [&](){return sqlite3_column_int64(stmt, 0);} ;

  auto name = [&](){ auto first = sqlite3_column_text (stmt, 1);
    std::size_t s = sqlite3_column_bytes (stmt, 1);
    return  s > 0 ? std::string ((const char*)first, s)
                  : std::string{};
  };
  auto value = [&]() {return sqlite3_column_double(stmt, 2);};

  std::cout << id() << ", " << name() << ", " << value() << std::endl;
  return true ;
}



int64_t key(not_null<sqlite3_stmt*> stmt)
{
  return sqlite3_column_int64(stmt, 0) ;
}

std::string value(not_null<sqlite3_stmt*> stmt)
{
  const char* first = (const char*)sqlite3_column_text (stmt, 1);
  std::size_t s = sqlite3_column_bytes (stmt, 1);
  return  s > 0 ? std::string (first, s) : std::string{};
}


void parameter(not_null<sqlite3_stmt*> stmt, int index, int64_t value)
{
  auto rc = sqlite3_bind_int64 (stmt, index, value);
  if (rc != SQLITE_OK) throw "TODO" ;
}

void parameter(not_null<sqlite3_stmt*> stmt, int index, double value)
{
  auto rc = sqlite3_bind_double (stmt, index, value);
  if (rc != SQLITE_OK) throw "TODO" ;
}

// real the same
void parameter(not_null<sqlite3_stmt*> stmt,
              int index,
              const std::string& value)
{
   auto rc = sqlite3_bind_text (stmt.get(), index,
                            value.c_str (), value.size (),
                            SQLITE_TRANSIENT);

   if (rc != SQLITE_OK) throw "TODO" ;
}
// blob the same, SQLITE_STATIC/TRANSIENT copy + owner



struct Transaction
{
  Transaction(not_null<sqlite3*> db) : _db{db}{
    execute(_db, "BEGIN TRANSACTION;") ;
  }
  ~Transaction() {
    if(_db) execute(_db, "ROLLBACK TRANSACTION;") ;
  }
  void commit() {
    if(_db) execute(_db, "COMMIT TRANSACTION;") ;
    _db = nullptr ;
  }

  Transaction (Transaction&&) =  default ;

  Transaction (Transaction&) =  delete ;
  Transaction& operator=(Transaction&) =  delete ;
  Transaction& operator=(Transaction&&) =  delete ;

private:  sqlite3* _db ;
};

constexpr const char* create_things()
{
  return R"~(BEGIN TRANSACTION ;
  CREATE TABLE things(id INTEGER PRIMARY KEY, name TEXT,value REAL);
  INSERT INTO things VALUES(1,'one', 1.1);
  INSERT INTO things VALUES(2,'two', 2.2);
  COMMIT TRANSACTION ;
  )~";
}



statement create_things2(not_null<sqlite3*> db) {
  Transaction transaction(db) ;
  execute(db, R"~(CREATE TABLE things
  (id INTEGER PRIMARY KEY, name TEXT,value REAL); )~");

  auto insert_thing = create_statement(db,
        "INSERT INTO things VALUES(@id,@name,@value);");
  // create the identity thing
  parameter(insert_thing.get(), 1, int64_t{0}) ;
  parameter(insert_thing.get(), 2, "") ;
  parameter(insert_thing.get(), 3, double{0.0}) ;
  run (insert_thing.get()) ;
  transaction.commit() ;
  // return createor
  return insert_thing ;
}


void main1()
{
  auto db = open_database(":memory:");
  auto add_thing =  create_things2(db.get());
  { Transaction transaction(db.get()) ;
    parameter(add_thing.get(), 1, int64_t{1}) ;
    parameter(add_thing.get(), 2, "first") ;
    parameter(add_thing.get(), 3, "second") ; // Mistake !!
    run(add_thing.get());
    transaction.commit() ;
  }
  auto stmt = create_statement(db.get(), "SELECT * FROM things;");
  run(stmt.get(), dump_current_row);
  run (stmt.get(), print_thing);
}


int main()
{
  main1();
}

