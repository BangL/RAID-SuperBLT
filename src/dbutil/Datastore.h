#pragma once

#include <string>

#include <stdint.h>

// The engine reads these through the vtable it knows from its own (MSVC) build, so the order of the virtual functions
// is part of the interface. In the MSVC ABI a virtual destructor is a single slot (the scalar deleting destructor),
// in the Itanium ABI of GCC/MinGW it is two (complete and deleting), which would move every function after it by one:
// the engine would call read() and land in write(). Outside MSVC the destructor is therefore replaced by a function
// in the same slot that does what the engine expects from it.
#ifdef _MSC_VER
#define BLT_DATASTORE_DESTRUCTOR(Class)
#else
#define BLT_DATASTORE_DESTRUCTOR(Class) \
	virtual void* msvc_scalar_deleting_destructor(unsigned int flags) override \
	{ \
		this->~Class(); \
		if (flags & 1) \
			::operator delete(this); \
		return this; \
	}
#endif

class BLTAbstractDataStore
{
  public:
#ifdef _MSC_VER
	virtual ~BLTAbstractDataStore()
	{
	}
#else
	virtual void* msvc_scalar_deleting_destructor(unsigned int flags) = 0;
#endif
	virtual size_t write(uint64_t position_in_file, uint8_t const* data, size_t length); // Stubbed with an abort
	virtual size_t read(uint64_t position_in_file, uint8_t* data, size_t length) = 0;
	virtual bool close() = 0;
	virtual size_t size() const = 0;
	virtual bool is_asynchronous() const = 0;
	virtual void set_asynchronous_completion_callback(void* /*dsl::LuaRef*/); // ignore this
	virtual uint64_t state(); // ignore this
	virtual bool good() const = 0;
};

class BLTFileDataStore : public BLTAbstractDataStore
{
  public:
	// Delete default crap
	BLTFileDataStore(const BLTFileDataStore&) = delete;
	BLTFileDataStore& operator=(const BLTFileDataStore&) = delete;

	static BLTFileDataStore* Open(std::string filePath);
	~BLTFileDataStore();
	BLT_DATASTORE_DESTRUCTOR(BLTFileDataStore)
	virtual size_t read(uint64_t position_in_file, uint8_t* data, size_t length) override;
	virtual bool close() override;
	virtual size_t size() const override;
	virtual bool is_asynchronous() const override;
	virtual bool good() const override;

  private:
	BLTFileDataStore() = default; // Used by Open, which can return null to indicate it didn't open properly
	int fd = -1;
	size_t file_size = 0;
};

class BLTStringDataStore : public BLTAbstractDataStore
{
  public:
	// Delete default crap
	BLTStringDataStore(const BLTStringDataStore&) = delete;
	BLTStringDataStore& operator=(const BLTStringDataStore&) = delete;

	explicit BLTStringDataStore(std::string contents);
	BLT_DATASTORE_DESTRUCTOR(BLTStringDataStore)
	virtual size_t read(uint64_t position_in_file, uint8_t* data, size_t length) override;
	virtual bool close() override;
	virtual size_t size() const override;
	virtual bool is_asynchronous() const override;
	virtual bool good() const override;

  private:
	std::string contents;
};
