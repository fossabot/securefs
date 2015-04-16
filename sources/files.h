#pragma once
#include "utils.h"
#include "streams.h"

#include <mutex>
#include <string>
#include <memory>
#include <functional>

namespace securefs
{
class FileBase
{
private:
    std::mutex m_lock;
    std::shared_ptr<HeaderBase> m_header;
    uint32_t m_flags[7];
    bool m_dirty;

private:
    void read_header();

protected:
    std::shared_ptr<StreamBase> m_stream;

    uint32_t get_root_page() const noexcept { return m_flags[4]; }
    void set_root_page(uint32_t value) noexcept
    {
        m_flags[4] = value;
        m_dirty = true;
    }
    uint32_t get_start_free_page() const noexcept { return m_flags[5]; }
    void set_start_free_page(uint32_t value) noexcept
    {
        m_flags[5] = value;
        m_dirty = true;
    }
    uint32_t get_num_free_page() const noexcept { return m_flags[6]; }
    void set_num_free_page(uint32_t value) noexcept
    {
        m_flags[6] = value;
        m_dirty = true;
    }

    /**
     * Subclasss should override this if additional flush operations are needed
     */
    virtual void subflush() {}

public:
    static const int REGULAR_FILE = 0, SYMLINK = 1, DIRECTORY = 2;

public:
    explicit FileBase(std::shared_ptr<StreamBase> stream, std::shared_ptr<HeaderBase> header)
        : m_header(header), m_stream(stream)
    {
        if (!m_stream || !m_header)
            NULL_EXCEPT();
        read_header();
    }
    virtual ~FileBase();
    DISABLE_COPY_MOVE(FileBase);

    uint32_t get_mode() const noexcept { return m_flags[0]; }
    void set_mode(uint32_t value) noexcept
    {
        m_flags[0] = value;
        m_dirty = true;
    }
    uint32_t get_uid() const noexcept { return m_flags[1]; }
    void set_uid(uint32_t value) noexcept
    {
        m_flags[1] = value;
        m_dirty = true;
    }
    uint32_t get_gid() const noexcept { return m_flags[2]; }
    void set_gid(uint32_t value) noexcept
    {
        m_flags[2] = value;
        m_dirty = true;
    }
    uint32_t get_nlink() const noexcept { return m_flags[3]; }
    void set_nlink(uint32_t value) noexcept
    {
        m_flags[3] = value;
        m_dirty = true;
    }

    void lock() { m_lock.lock(); }
    void unlock() { m_lock.unlock(); }

    virtual int type() const noexcept = 0;
    void flush();
    void stat(struct stat* st)
    {
        m_stream->stat(st);
        if (st)
        {
            st->st_uid = get_uid();
            st->st_gid = get_gid();
            st->st_nlink = get_nlink();
            st->st_mode = get_mode();
            st->st_size = m_stream->size();
            auto blk_sz = m_stream->optimal_block_size();
            if (blk_sz > 1 && blk_sz < std::numeric_limits<decltype(st->st_blksize)>::max())
            {
                st->st_blksize = static_cast<decltype(st->st_blksize)>(blk_sz);
                st->st_blocks = (st->st_size + st->st_blksize - 1) / st->st_blksize;
            }
        }
    }
    void fsync() { return m_stream->fsync(); }
};

class RegularFile : public FileBase
{
public:
    explicit RegularFile(std::shared_ptr<StreamBase> stream, std::shared_ptr<HeaderBase> header)
        : FileBase(std::move(stream), std::move(header))
    {
    }
    int type() const noexcept override { return FileBase::REGULAR_FILE; }
    length_type read(void* output, offset_type off, length_type len)
    {
        return this->m_stream->read(output, off, len);
    }
    void write(const void* input, offset_type off, length_type len)
    {
        return this->m_stream->write(input, off, len);
    }
    length_type size() const noexcept { return m_stream->size(); }
    void truncate(length_type new_size) { return m_stream->resize(new_size); }
};

class Symlink : public FileBase
{
public:
    explicit Symlink(std::shared_ptr<StreamBase> stream, std::shared_ptr<HeaderBase> header)
        : FileBase(std::move(stream), std::move(header))
    {
    }
    int type() const noexcept override { return FileBase::SYMLINK; }
    std::string get()
    {
        std::string result(m_stream->size(), 0);
        auto rc = m_stream->read(&result[0], 0, result.size());
        result.resize(rc);
        return result;
    }
    void set(const std::string& path) { m_stream->write(path.data(), 0, path.size()); }
};

class Directory : public FileBase
{
public:
    static const size_t MAX_FILENAME_LENGTH = 255;

public:
    explicit Directory(std::shared_ptr<StreamBase> stream, std::shared_ptr<HeaderBase> header)
        : FileBase(std::move(stream), std::move(header))
    {
    }
    int type() const noexcept override { return FileBase::DIRECTORY; }

    virtual bool get_entry(const std::string& name, id_type& id, int& type) = 0;
    virtual bool add_entry(const std::string& name, const id_type& id, int type) = 0;
    virtual bool remove_entry(const std::string& name) = 0;

    typedef std::function<void(const std::string&, const id_type&, int)> callback;
    virtual void iterate_over_entries(callback cb) = 0;
};

std::shared_ptr<Directory> make_directory(std::shared_ptr<StreamBase> stream,
                                          std::shared_ptr<HeaderBase> header);
}
