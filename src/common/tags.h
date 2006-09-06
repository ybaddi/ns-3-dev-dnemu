/* -*-    Mode:C++; c-basic-offset:4; tab-width:4; indent-tabs-mode:f -*- */
/*
 * Copyright (c) 2006 INRIA
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#ifndef TAGS_H
#define TAGS_H

#include <stdint.h>
#include <ostream>
#include <vector>

namespace ns3 {

template <typename T>
class TagPrettyPrinter;

class Tags {
public:
    inline Tags ();
    inline Tags (Tags const &o);
    inline Tags &operator = (Tags const &o);
    inline ~Tags ();

    template <typename T>
    void add (T const*tag);

    template <typename T>
    bool remove (T *tag);

    template <typename T>
    bool peek (T *tag) const;

    void prettyPrint (std::ostream &os);

    inline void removeAll (void);

    enum {
        SIZE = 16
    };
private:
    struct TagData {
        struct TagData *m_next;
        uint32_t m_id;
        uint32_t m_count;
        uint8_t m_data[Tags::SIZE];
    };
    class UidFactory {
    public:
        static uint32_t create (void);
    };

    bool remove (uint32_t id);
    struct Tags::TagData *allocData (void);
    void freeData (struct TagData *data);

    static struct Tags::TagData *gFree;
    static uint32_t gN_free;

    struct TagData *m_next;
};

/**
 * \brief pretty print packet tags
 * 
 * This class is used to register a pretty-printer
 * callback function to print in a nice user-friendly
 * way the content of the target type. To register
 * such a type, all you need to do is instantiate
 * an instance of this type any number of times (at
 * least once). Typical users will create static global
 * variable of this type and construct it with
 * the proper function pointer.
 */
template <typename T>
class TagPrettyPrinter {
public:
	/**
	 * \param fn a function which can pretty-print an instance
	 *        of type T in the output stream.
	 */
    TagPrettyPrinter<T> (void(*fn) (T *, std::ostream &));
private:
    TagPrettyPrinter<T> ();
    static void prettyPrintCb (uint8_t *buf, std::ostream &os);
    static void(*gPrettyPrinter) (T *, std::ostream &);
};

class TagsPrettyPrinterRegistry {
public:
    static void record (uint32_t uid, void (*cb) (uint8_t buf[Tags::SIZE], std::ostream &os));
    static void prettyPrint (uint32_t uid, uint8_t buf[Tags::SIZE], std::ostream &os);
private:
    typedef std::vector<std::pair<uint32_t, void (*) (uint8_t [Tags::SIZE], std::ostream &)> > PrettyPrinters;
    typedef std::vector<std::pair<uint32_t, void (*) (uint8_t [Tags::SIZE], std::ostream &)> >::iterator PrettyPrintersI;
    static PrettyPrinters gPrettyPrinters;
};


}; // namespace ns3



/**************************************************************
     An implementation of the templates defined above
 *************************************************************/
#include <cassert>
#include <string.h>

namespace ns3 {

/**
 * The TypeUid class is used to create a mapping Type --> uid
 * Of course, this class is not perfect: the value of the uid
 * associated to a given type could change accross multiple 
 * runs of the same program on the same platform or accross
 * multiple platforms. There exist no generic portable
 * workaround/solution to this problem also known as 
 * "type id management". The only other reliable solution 
 * is to ask programmers to assign themselves a uid to each 
 * type but this is painful from a management perspective.
 *
 * So, for now, this class is good enough provided you do
 * not try to serialize to permanent storage the type uids
 * generated by this class. Just don't try to do it. It might
 * seem to work but it will fail spectacularily in certain
 * use-cases and you will cry from debugging this. Hear me ?
 */
template <typename T>
class TypeUid {
public:
    static const uint32_t getUid (void);
private:
    T realType;
};

template <typename T>
const uint32_t TypeUid<T>::getUid (void)
{
    static const uint32_t uid = Tags::UidFactory::create ();
    return uid;
}



/**
 * Implementation of the TagPrettyPrinter registration class.
 * It records a callback with the TagPrettyPrinterRegistry.
 * This callback performs type conversion before forwarding
 * the call to the user-provided function.
 */
template <typename T>
TagPrettyPrinter<T>::TagPrettyPrinter (void(*prettyPrinter) (T *, std::ostream &))
{
    assert (sizeof (T) <= Tags::SIZE);
    gPrettyPrinter  = prettyPrinter;
    TagsPrettyPrinterRegistry::record (TypeUid<T>::getUid (),
                      &TagPrettyPrinter<T>::prettyPrintCb);
}
template <typename T>
void 
TagPrettyPrinter<T>::prettyPrintCb (uint8_t *buf, std::ostream &os)
{
    assert (sizeof (T) <= Tags::SIZE);
    T *tag = reinterpret_cast<T *> (buf);
    (*gPrettyPrinter) (tag, os);
}

template <typename T>
void (*TagPrettyPrinter<T>::gPrettyPrinter) (T *, std::ostream &) = 0;




template <typename T>
void 
Tags::add (T const*tag)
{
    assert (sizeof (T) <= Tags::SIZE);
    uint8_t const*buf = reinterpret_cast<uint8_t const*> (tag);
    // ensure this id was not yet added
    for (struct TagData *cur = m_next; cur != 0; cur = cur->m_next) {
        assert (cur->m_id != TypeUid<T>::getUid ());
    }
    struct TagData *newStart = allocData ();
    newStart->m_count = 1;
    newStart->m_next = 0;
    newStart->m_id = TypeUid<T>::getUid ();
    memcpy (newStart->m_data, buf, sizeof (T));
    newStart->m_next = m_next;
    m_next = newStart;
}

template <typename T>
bool
Tags::remove (T *tag)
{
    assert (sizeof (T) <= Tags::SIZE);
    return remove (TypeUid<T>::getUid ());
}

template <typename T>
bool
Tags::peek (T *tag) const
{
    assert (sizeof (T) <= Tags::SIZE);
    uint8_t *buf = reinterpret_cast<uint8_t *> (tag);
    for (struct TagData *cur = m_next; cur != 0; cur = cur->m_next) {
        if (cur->m_id == TypeUid<T>::getUid ()) {
            /* found tag */
            memcpy (buf, cur->m_data, sizeof (T));
            return true;
        }
    }
    /* no tag found */
    return false;
}

Tags::Tags ()
    : m_next ()
{}

Tags::Tags (Tags const &o)
    : m_next (o.m_next)
{
    if (m_next != 0) {
        m_next->m_count++;
    }
}

Tags &
Tags::operator = (Tags const &o)
{
    // self assignment
    if (m_next == o.m_next) {
        return *this;
    }
    removeAll ();
    m_next = o.m_next;
    if (m_next != 0) {
        m_next->m_count++;
    }
    return *this;
}

Tags::~Tags ()
{
    removeAll ();
}

void
Tags::removeAll (void)
{
    struct TagData *prev = 0;
    for (struct TagData *cur = m_next; cur != 0; cur = cur->m_next) {
        cur->m_count--;
        if (cur->m_count > 0) {
            break;
        }
        if (prev != 0) {
            freeData (prev);
        }
        prev = cur;
    }
    if (prev != 0) {
        freeData (prev);
    }
    m_next = 0;
}


}; // namespace ns3

#endif /* TAGS_H */
