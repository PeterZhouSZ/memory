// Copyright (C) 2015 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#ifndef FOONATHAN_MEMORY_MEMORY_ARENA_HPP_INCLUDED
#define FOONATHAN_MEMORY_MEMORY_ARENA_HPP_INCLUDED

#include "detail/utility.hpp"
#include "config.hpp"
#include "error.hpp"

namespace foonathan { namespace memory
{
    struct memory_block
    {
        void *memory;
        std::size_t size;

        memory_block() FOONATHAN_NOEXCEPT
        : memory_block(nullptr, size) {}

        memory_block(void *mem, std::size_t size) FOONATHAN_NOEXCEPT
        : memory(mem), size(size) {}

        memory_block(void *begin, void *end) FOONATHAN_NOEXCEPT
        : memory_block(begin, static_cast<char*>(end) - static_cast<char*>(begin)) {}
    };

    namespace detail
    {
        // stores memory block in an intrusive linked list and allows LIFO access
        class memory_block_stack
        {
        public:
            memory_block_stack() FOONATHAN_NOEXCEPT
            : head_(nullptr) {}

            ~memory_block_stack() FOONATHAN_NOEXCEPT {}

            memory_block_stack(memory_block_stack &&other) FOONATHAN_NOEXCEPT
            : head_(other.head_)
            {
                other.head_ = nullptr;
            }

            memory_block_stack& operator=(memory_block_stack &&other) FOONATHAN_NOEXCEPT
            {
                memory_block_stack tmp(detail::move(other));
                swap(*this, tmp);
                return *this;
            }

            friend void swap(memory_block_stack &a, memory_block_stack &b) FOONATHAN_NOEXCEPT
            {
                detail::adl_swap(a.head_, b.head_);
            }

            // the raw allocated block returned from an allocator
            using allocated_mb = memory_block;

            // the inserted block slightly smaller to allow for the fixup value
            using inserted_mb = memory_block;

            // pushes a memory block
            void push(allocated_mb block) FOONATHAN_NOEXCEPT;

            // pops a memory block and returns the original block
            allocated_mb pop() FOONATHAN_NOEXCEPT;

            // steals the top block from another stack
            void steal_top(memory_block_stack &other) FOONATHAN_NOEXCEPT;

            // returns the last pushed() inserted memory block
            inserted_mb top() const FOONATHAN_NOEXCEPT;

            bool empty() const FOONATHAN_NOEXCEPT
            {
                return head_ == nullptr;
            }

        private:
            struct node;
            node *head_;
        };
    } // namespace detail

    template <class BlockAllocator>
    class memory_arena : FOONATHAN_EBO(BlockAllocator)
    {
    public:
        using allocator_type = BlockAllocator;

        explicit memory_arena(allocator_type allocator = allocator_type{}) FOONATHAN_NOEXCEPT
        : allocator_type(detail::move(allocator)),
          no_used_(0u), no_cached_(0u)
        {}

        memory_arena(memory_arena &&other) FOONATHAN_NOEXCEPT
        : allocator_type(detail::move(other)),
          used_(detail::move(other)), cached_(detail::move(other)),
          no_used_(other.no_used_), no_cached_(other.no_cached_)
        {
            other.no_used_ = other.no_cached_ = 0;
        }

        ~memory_arena() FOONATHAN_NOEXCEPT
        {
            // push all cached to used_ to reverse order
            while (!cached_.empty())
                used_.steal_top(cached_);
            // now deallocate everything
            while (!used_.empty())
                allocator_type::deallocate_block(used_.pop());
        }

        memory_arena& operator=(memory_arena &&other) FOONATHAN_NOEXCEPT
        {
            memory_arena tmp(detail::move(other));
            swap(*this, tmp);
            return *this;
        }

        friend void swap(memory_arena &a, memory_arena &b) FOONATHAN_NOEXCEPT
        {
            detail::adl_swap(a.used_, b.used_);
            detail::adl_swap(a.cached_, b.cached_);
            detail::adl_swap(a.no_used_, b.no_used_);
            detail::adl_swap(a.no_cached_, b.no_cached_);
        }

        memory_block allocate_block()
        {
            if (capacity() == size())
                used_.push(allocator_type::allocate_block());
            else
            {
                used_.steal_top(cached_);
                --no_cached_;
            }
            ++no_used_;
            return used_.top();
        }

        void deallocate_block() FOONATHAN_NOEXCEPT
        {
            --no_used_;
            ++no_cached_;
            cached_.steal_top(used_);
        }

        void shrink_to_fit() FOONATHAN_NOEXCEPT
        {
            detail::memory_block_stack to_dealloc;
            // pop from cache and push to temporary stack
            // this revers order
            while (!cached_.empty())
                to_dealloc.steal_top(cached_);
            // now dealloc everything
            while (!to_dealloc.empty())
                allocator_type::deallocate_block(to_dealloc.pop());
            no_cached_ = 0u;
        }

        std::size_t capacity() const FOONATHAN_NOEXCEPT
        {
            FOONATHAN_MEMORY_ASSERT(bool(no_cached_) != cached_.empty());
            return no_cached_ + no_used_;
        }

        std::size_t size() const FOONATHAN_NOEXCEPT
        {
            FOONATHAN_MEMORY_ASSERT(bool(no_used_) != used_.empty());
            return no_used_;
        }

        std::size_t next_block_size() const FOONATHAN_NOEXCEPT
        {
            return allocator_type::next_block_size();
        }

        allocator_type& get_allocator() FOONATHAN_NOEXCEPT
        {
            return *this;
        }

    private:
        detail::memory_block_stack used_, cached_;
        std::size_t no_used_, no_cached_;
    };
}} // namespace foonathan::memory

#endif // FOONATHAN_MEMORY_MEMORY_ARENA_HPP_INCLUDED
