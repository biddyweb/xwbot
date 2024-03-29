/*
 * Copyright (c) 2011, artemka
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * xwbot
 * Created on: Jun 6, 2011
 *     Author: artemka
 *
 */

#undef DEBUG_PRFX

#include <x_config.h>

#if TRACE_XOBJECT_ON
#define DEBUG_PRFX "[xobject] "
#endif

#include "x_types.h"

#if defined( WIN32 )
#include <windows.h>
#else
#include <unistd.h>
#include <alloca.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#endif

#include "x_lib.h"
#include "x_string.h"
#include "x_utils.h"
#include "x_obj.h"

static void
x_object_set_parent(x_object *child, x_object *parent);

static x_object *
__x_object_get_child_nolock(x_object *xparent, const x_string_t cname);

static __inline__ void
x_object_io_update(x_object *thiz, x_obj_listener_t **p,
                   x_obj_listener_t **prev, int err);

/**
 * @brief x_object_set_write_handler
 * @param from
 * @param to
 * @deprecated Use 'write_out' api...
 */
void
x_object_set_write_handler(x_object *from, x_object *to)
{
  ENTER;
  if (from && to)
  {
      from->write_handler = to;
      _REFGET(to);
  }
  EXIT;
}

/**
 * @brief x_object_try_write
 * @param xobj
 * @param buf
 * @param len
 * @param attrs
 * @return
 * @deprecated Use x_object_try_writev()
 */
int
x_object_try_write(x_object *xobj, void *buf, int len, x_obj_attr_t *attrs)
{
  int res = -1;
  ENTER;
  if (xobj->objclass
          && xobj->objclass->try_write)
    {
      res = xobj->objclass->try_write(xobj, buf, len, attrs);
    }
  EXIT;
  return res;
}

/**
 * @brief x_object_try_writev
 * @param xobj
 * @param iov
 * @param iovcnt
 * @param attrs
 * @return
 */
int
x_object_try_writev(x_object *xobj, const struct iovec *iov, int iovcnt,
    x_obj_attr_t *attrs)
{
  int res = -1;
  ENTER;
  if (xobj->objclass->try_writev)
    {
      res = xobj->objclass->try_writev(xobj, iov, iovcnt, attrs);
    }
  EXIT;
  return res;
}

/**
 * @brief x_object_try_writev2
 * @param xobj
 * @param slot
 * @param iov
 * @param iovcnt
 * @param attrs
 * @return
 */
int
x_object_try_writev2(x_object *xobj, int slot, const struct iovec *iov, int iovcnt,
    x_obj_attr_t *attrs)
{
  int res = -1;
  ENTER;
  if (xobj->objclass->try_writev2)
    {
      res = xobj->objclass->try_writev2(xobj, slot, iov, iovcnt, attrs);
    }
  EXIT;
  return res;
}

/**
 * @brief x_object_init
 * @param xobj
 */
void
x_object_init(x_object *xobj)
{
//  XOBJ_CS_INIT(xobj);
  XOBJ_CS_LOCK(xobj);
  xref_init(&xobj->refcount);
  attr_list_init(&xobj->attrs);
  x_string_trunc(&xobj->content);
  XOBJ_CS_UNLOCK(xobj);
  return;
}

/**
 * @brief __x_object_alloc
 * @param extra
 * @return
 */
static x_object *
__x_object_alloc(unsigned int extra)
{
  x_object *xobj = NULL;
  size_t space = sizeof(x_object) + extra;
  TRACE("Allocating '%u' bytes\n", space);
  /* ENTER;*/
  if ((xobj = (x_object *) x_malloc(space)) != NULL)
    {
      TRACE("\n");
      x_memset((void *) xobj, 0, space);
      xobj->extra = extra;
      x_object_init(xobj);
    }
  else
    {
      TRACE("\n");
    }
  /*EXIT;*/
  return xobj;
}

/**
 * @brief x_object_find
 * @param starto
 * @param hints
 * @return
 */
x_object *
x_object_find(x_object *starto, x_obj_attr_t *hints)
{
  x_object *o = NULL, *o_;
  x_object *parent = _PARNT(starto);

  if (!starto)
    return NULL;

  if (parent)
    XOBJ_CS_LOCK(parent);

  for (o_ = starto; o_; o_ = _NXT(o_))
    {
      TRACE("Matchnig object '%s'\n", _GETNM(o_));
      if (x_object_match(o_, hints))
        {
          o = o_;
          break;
        }
    }

  if (parent)
    XOBJ_CS_UNLOCK(parent);

  return o;
}

/**
 * @brief x_object_find_child
 * @param parent
 * @param hints
 * @return
 */
x_object *
x_object_find_child(x_object *parent, x_obj_attr_t *hints)
{
  x_object *o = NULL, *o_;

  XOBJ_CS_LOCK(parent);

  for (o_ = __x_object_get_child_nolock(parent, NULL); o_; o_ = _SBL(o_))
    {
      TRACE("Matchnig object '%s'\n", _GETNM(o_));
      if (x_object_match(o_, hints))
        {
          o = o_;
          break;
        }
    }

  XOBJ_CS_UNLOCK(parent);

  return o;
}

/**
 * Allocate new xobject
 * @brief x_object_new_ns_match
 * @param p_name
 * @param ns
 * @param hints
 * @param proto_on_fail indicates should object factory allocate default object for unregistered class requests.
 * @return
 */
x_object *
x_object_new_ns_match(const x_string_t p_name, const x_string_t ns,
    x_obj_attr_t *hints, BOOL proto_on_fail)
{
  x_object *xobj;
  x_objectclass *oclass;
  unsigned int extra = 0;
  x_string _name =
    { 0, 0, 0 };
  char *name;

  /*ENTER;*/

  if (!p_name)
    {
      EXIT;
      return NULL;
    }

  x_string_write(&_name, p_name, x_strlen(p_name));
  str_swap_chars(_name.cbuf, '\n', 0);
  name = _name.cbuf;

  if ((oclass = x_obj_get_class(name, ns, hints, proto_on_fail)) != NULL)
    extra = oclass->psize;

  if (!(proto_on_fail || oclass))
    {
      EXIT;
      return NULL;
    }

  TRACE("Allocating %d bytes for '%s'(len=%d)\n",
      extra, name, (int)x_strlen(name));

  xobj = __x_object_alloc(extra);
  TRACE("OK! Allocated at %p\n", xobj);

  if (!xobj)
    {
      EXIT;
      return NULL;
    }

  TREE_NODE(xobj)->type = TYPE_OBJECT_NODE;

  _SETNM(xobj, name);
  xobj->objclass = oclass;
  /**
   * Hack for C++ binding
   */
  xobj->__vptr = (void *) oclass;

  /*
   TRACE("New object(%p) name(%p)='%s'\n",xobj,&xobj->entry.name,
   x_object_get_name(xobj
   ));
   */
  /* callback */
  if (xobj->objclass && xobj->objclass->on_create)
    xobj->objclass->on_create(xobj);

  /*EXIT;*/
  return xobj;
}

/**
 * @brief x_object_new_ns
 * @param name
 * @param ns
 * @return
 */
x_object *
x_object_new_ns(const x_string_t name, const x_string_t ns)
{
  return x_object_new_ns_match(name, ns, NULL, TRUE);
}

/**
 * @deprecated Use x_object_new_ns() instead.
 */
x_object *
x_object_new(const x_string_t name)
{
  return x_object_new_ns(name, NULL);
}

/**
 * @brief __x_object_insert_into_tree
 * @param parent
 * @param xobj
 * @return
 */
static int
__x_object_insert_into_tree(x_object *parent, x_object *xobj)
{
  x_object *xt, *xp;

  ENTER;

  if (!parent || !xobj)
    return -1;

  x_object_set_parent(xobj, parent);

  xp = TNODE_TO_XOBJ(TREE_NODE(parent)->sons);

  if (xp == NULL)
    {
      TREE_NODE(parent)->sons = TREE_NODE(xobj);
    }
  else
    {
      /* update siblings */
      do
        {
          xt = xp;
        }
      while ((xp = _SBL(xp)) != NULL);
      TREE_NODE(xt)->sibling = TREE_NODE(xobj);

      /* update nexts */
      for (xt = NULL, xp = __x_object_get_child_nolock(parent, _GETNM(xobj));
          xp; xp = _NXT(xp)
          )
        xt = xp;

      if (xt)
        TREE_NODE(xt)->next = TREE_NODE(xobj);
    }

  TREE_NODE(xobj)->next = NULL;
  TREE_NODE(xobj)->sibling = NULL;

  // update child count
  TREE_NODE(parent)->child_count++;
  EXIT;

  return 0;
}

/**
 * @brief __x_object_remove_from_treenode
 * @param parent
 * @param xobj
 * @return
 */
static int
__x_object_remove_from_treenode(x_object *parent, x_object *xobj)
{
  x_object *xt, *xp;

  ENTER;

  if (!parent || !xobj)
    return -1;

  x_object_set_parent(xobj, NULL);

  xp = TNODE_TO_XOBJ(TREE_NODE(parent)->sons);

  if (xp != NULL)
    {
      /* update nexts */
      for (xt = NULL,
           xp = __x_object_get_child_nolock(parent, _GETNM(xobj));
           xp && (xp != xobj); xp = _NXT(xp)
      )
        xt = xp;

      if (xt && (xp == xobj))
        TREE_NODE(xt)->next = TREE_NODE(xobj)->next;

      TREE_NODE(xobj)->next = NULL;

      /* update siblings */
      for (xt = NULL,
           xp = TNODE_TO_XOBJ(TREE_NODE(parent)->sons);
           xp && (xp != xobj); xp = _SBL(xp))
        xt = xp;

      if (xt && (xp == xobj))
        TREE_NODE(xt)->sibling = TREE_NODE(xobj)->sibling;

      TREE_NODE(xobj)->sibling = NULL;

    }

  // update child count
  TREE_NODE (parent)->child_count <= 0 ? TREE_NODE(parent)->child_count += 0 :
      TREE_NODE(parent)->child_count--;

  /* set dirty */
  x_object_set_cr(parent, x_object_get_cr(parent) | XOBJ_CR_DIRTY);
  x_object_set_cr(xobj, x_object_get_cr(xobj) | XOBJ_CR_DIRTY);

  EXIT;

  return 0;
}

/**
 * This explicitly notifies path listeners on object changes.
 *
 * @todo This event should occur on x_path but not on object itself \
 * because of listeners are registered to listen for x_path events.
 */
x_object *
x_object_touch(x_object *xobj)
{
  ENTER;
  x_path_listeners_notify(x_object_get_name(xobj), xobj);
  EXIT;
  return xobj;
}

/**
 * Returns object at path 'path' relative to 'ctx'
 *
 * @brief x_object_from_path
 * @param ctx
 * @param path
 * @return
 */
x_object *
x_object_from_path(const x_object *ctx, const x_string_t path)
{
  x_object *_obj = (x_object *) ctx;
  char *p2, *end;
  size_t len;
  char *p1 = NULL;
  char *_p1 = NULL;

  ENTER;

  BUG_ON(!path);

  len = x_strlen(path) + 1;
  _p1 = p1 = x_malloc((len));
  x_memcpy(p1, path, len);

  end = &p1[len];

  do
    {
      p2 = p1 + strcspn(p1, "/:\n\t\v\f\r");

      if (p2 == p1)
        {
          break;
        }

      *p2 = '\0';
      _obj = _CHLD(_obj, p1);
      p1 = p2 + 1;
    }
  while (_obj && p2 < end);

  EXIT;

  if (_p1)
    x_free(_p1);

  return (_obj ? _obj : (x_object *) ctx);
}

/**
 * @brief x_object_add_path
 * @param ctx
 * @param path
 * @param obj
 * @return
 */
x_object *
x_object_add_path(x_object *ctx, const x_string_t path, x_object *obj)
{
  x_object *parent = x_object_mkpath(ctx, path);

  if (parent && obj)
    _INS(parent, obj);

  return parent;
}

/**
 * @brief x_object_mkpath
 * @param ctx
 * @param path
 * @return
 */
x_object *
x_object_mkpath(const x_object *ctx, const x_string_t path)
{
  x_object *obj = (x_object *) ctx;
  x_object *_obj = NULL;
  char *p2, *end;
  size_t len;
  char *sptr;
  char *p1 = NULL;

  /*ENTER;*/
  /*  TRACE("path='%s'\n",path); */
  BUG_ON(!path);

  len = x_strlen(path) + 1;
  p1 = x_malloc((len));
  /* save pointer */
  sptr = p1;
  x_memcpy(p1, path, len);
  p1[len] = '\0';

  end = &p1[len];

  do
    {
      p2 = p1 + strcspn(p1, "/:");
      if (p2 == p1)
        break;
      *p2 = '\0';

      _obj = _CHLD(obj, p1);
      if (!_obj)
        {
          _obj = _GNEW(p1,NULL);
          /*          TRACE("Adding '%s' to '%s'\n",x_object_get_name(_obj),x_object_get_name(obj));*/
          _INS(obj, _obj);
        }

      p1 = p2 + 1;
      obj = _obj;
    }
  while (p2 < end);

  if (sptr)
    x_free(sptr);

  /*EXIT;*/
  return obj;
}

/**
 * @brief x_object_print_path
 * @param obj
 * @param depth
 */
void
x_object_print_path(x_object *obj, int depth)
{
  struct ht_cell *cell;
  int i = depth;
  x_object *tmp;
  char buf[1024], *p = buf;

  i = i<sizeof(buf)-1 ? i:sizeof(buf)-1;

  while (--i >= 0)
    *p++ = '\t';
  *p = 0;

  printf("%s %s {\n", buf, _GETNM(obj));
  for (cell = obj->attrs.next; cell; cell = cell->next)
    printf("%s\t%s='%s' \n", buf, cell->key, cell->val);
  printf("%s }\n", buf);

  for (tmp = _CHLD(obj, NULL); tmp; tmp = _SBL(tmp)
  )
  {
#ifdef DEBUG
      if (tmp < (x_object*)0x100)
      {
          TRACE("%p\n",tmp);
          BUG();
      }
#endif
    x_object_print_path(tmp, depth + 1);
  }
}

/**
 * @ingroup XOBJECT
 *
 */
int
x_object_to_path(x_object *obj, x_string_t pathbuf, unsigned int bufsiz)
{
  int i = 0, k;

  if (!obj)
    return 0;

  i = x_object_to_path(x_object_get_parent(obj), pathbuf, bufsiz);
  if (i >= 0)
    {
      k = x_snprintf(pathbuf + i, bufsiz - i, "/%s", x_object_get_name(obj));
      if (k >= 0)
        {
          i += k;
        }
    }
  return i;
}

/**
 * Make alias for xobj in the parent sub-tree
 *
 * @brief x_object_make_alias
 * @param xobj
 * @param parent
 * @param alias_name
 * @return
 */
x_alias *
x_object_make_alias(x_object *xobj, x_object *parent,
    const x_string_t alias_name)
{
  x_alias *xalias = NULL;

  if (xobj == NULL)
    return NULL;

  if ((xalias = (x_alias *) x_malloc(sizeof(x_alias))) != NULL)
    {
      x_memset((void *) xalias, 0, sizeof(x_alias));
      xalias->target = TREE_NODE(xobj);
      TREE_NODE(xalias)->type = TYPE_ALIAS_NODE;

      _SETNM(X_OBJECT(xalias), alias_name);

      if (__x_object_insert_into_tree(parent, X_OBJECT(xalias)))
        {
          x_free((void *) _GETNM(X_OBJECT(xalias)));
          x_free(xalias);
        }
      else
        {
          _REFGET(parent);
#pragma message ("is it OK that alias doesn't have it's own refcount ? \
    or may be it is right to increment @xobj refcount like below ?")
          /* x_object_ref_get(xobj); */
        }
    }

  return xalias;
}

/**
 * @brief x_object_remove_from_parent
 * @param xobj
 */
void
x_object_remove_from_parent(x_object *xobj)
{
  x_object *msg;
  int id;
  char buf[24];
  x_object *parent = _PARNT(xobj);

  /* emit remove signals */
  if (xobj->objclass && xobj->objclass->on_remove)
    xobj->objclass->on_remove(xobj);

  if (parent)
    {
      id = x_object_get_index(xobj);

      if (parent->objclass && parent->objclass->on_child_remove)
        {
          parent->objclass->on_child_remove(parent, xobj);
        }

      XOBJ_CS_LOCK(parent);
      XOBJ_CS_LOCK(xobj);

      __x_object_remove_from_treenode(parent, xobj);

      XOBJ_CS_UNLOCK(xobj);
      XOBJ_CS_UNLOCK(parent);

      /* notify parent listeners */
      msg = _GNEW("$message",NULL);
      _ASET(msg, "subject", "child-remove");
      _ASET(msg, "child-name", _GETNM(xobj));

      x_snprintf(buf, sizeof(buf) - 1, "%d", id);

      _ASET(msg, "child-index", buf);
      _MCAST(parent, msg);

      /* drop message */
      _REFPUT(msg, NULL);

    }

  _REFPUT(xobj, NULL);

  if (parent)
    _REFPUT(parent, NULL);

}

/**
 * Append child but don't fire any events
 *
 * @brief x_object_append_child_no_cb
 * @param parent
 * @param child
 * @return
 */
int
x_object_append_child_no_cb(x_object *parent, x_object *child)
{
  ENTER;

  if (!parent)
    {
      return -1;
    }

  XOBJ_CS_LOCK(parent);
  XOBJ_CS_LOCK(child);

  if (__x_object_insert_into_tree(parent, child))
    {
      XOBJ_CS_UNLOCK(child);
      XOBJ_CS_UNLOCK(parent);
      return -1;
    }

  _REFGET(parent);
  _REFGET(child);

  /* set bus */
  child->bus = parent->bus;

  /* set dirty */
  x_object_set_cr(parent, x_object_get_cr(parent) | XOBJ_CR_DIRTY);

  XOBJ_CS_UNLOCK(child);
  XOBJ_CS_UNLOCK(parent);

  EXIT;
  return 0;
}

/**
 * Appends child object
 *
 * @brief x_object_append_child
 * @param parent
 * @param child
 * @return
 */
int
x_object_append_child(x_object *parent, x_object *child)
{
  x_object *msg;
  char buf[24];

  ENTER;

  if (x_object_append_child_no_cb(parent,child) == -1)
    {
      return -1;
    }


  if (parent->objclass && parent->objclass->on_child_append)
    parent->objclass->on_child_append(parent, child);

  /* emit 'append' signals */
  if (child->objclass && child->objclass->on_append)
    child->objclass->on_append(child, parent);

  /* notify path listeners */
  /* ... */
  x_path_listeners_notify(_GETNM(child), child);

  /* notify parent listeners */
  msg = _GNEW("$message",NULL);
  _ASET(msg, "subject", "child-append");
  _ASET(msg, _XS("child-name"), _GETNM(child));

  x_snprintf(buf, sizeof(buf) - 1, "%d", x_object_get_index(child));
  _ASET(msg, "child-index", buf);

  _MCAST(parent, msg);

  /* drop message */
  _REFPUT(msg, NULL);

  EXIT;
  return 0;
}

/**
 * @brief x_object_get_name
 * @param xobj
 * @return
 */
const x_string_t
x_object_get_name(const x_object *xobj)
{
  TRACE("xobj(%p)\n", xobj);

//  BUG_ON((TREE_NODE(xobj)->name == 0x0));

  return TREE_NODE(xobj)->name;
}

/**
 * @brief x_object_set_name
 * @param xobj
 * @param name
 */
void
x_object_set_name(x_object *xobj, const x_string_t name)
{
  tree_node_t *tnode = TREE_NODE(xobj);
  size_t len;

  TRACE("xobj(%p), name(%s)\n", xobj, name);

  BUG_ON(!name);
  len = x_strlen(name) + 1;

  XOBJ_CS_LOCK(xobj);

  tnode->name = x_realloc(tnode->name, (len));
  BUG_ON(!tnode->name);

  x_memcpy(tnode->name, name, len);

  tnode->name[len] = '\0';

  XOBJ_CS_UNLOCK(xobj);

}

/**
 * Makes object's simple copy (no callbacks just stub).
 * Use this as data containers (i.e. messages)
 */
x_object *
x_object_copy(x_object *o)
{
  x_object *msg;
  ENTER;

  msg = _GNEW("$message",NULL);
  BUG_ON(!msg);

  XOBJ_CS_LOCK(o);
  _SETNM(msg, _GETNM(o));
  x_object_default_assign_cb(msg, &o->attrs);
  x_string_write(&msg->content, o->content.cbuf, o->content.pos);
  XOBJ_CS_UNLOCK(o);

  EXIT;
  return msg;
}

/**
 * Makes a complete object's tree copy (no callbacks just tree structre)
 */
x_object *
x_object_full_copy(x_object *o)
{
  x_object *msg,*tmp;
  ENTER;

  msg = _CPY(o);

  // append childs
  for (tmp = _CHLD(o,NULL);tmp;tmp=_SBL(tmp))
    {
      x_object_append_child_no_cb(msg,x_object_full_copy(tmp));
    }

  EXIT;
  return msg;
}

/*
 * Get Child object
 * @brief __x_object_get_child
 * @param xparent
 * @param cname
 * @param lock
 * @return
 */
static x_object *
__x_object_get_child(x_object *xparent, const x_string_t cname, int lock)
{
    tree_node_t *tn = NULL;
  x_object *_xobj = NULL, *xobj = NULL;
  const char *_name;

  if(!xparent)
  {
      TRACE("Invalid param: xparent==NULL \n");
      return NULL;
  }

  tn = TREE_NODE(xparent);
  if (!tn)
  {
    return NULL;
  }
  xobj = TNODE_TO_XOBJ(tn->sons);

  /*  ENTER;*/
  if (!cname)
    {
      EXIT;
      return xobj;
    }

  if (lock)
    XOBJ_CS_LOCK(xparent);

#if 0
  while (xobj != NULL && NEQ(x_object_get_name(xobj), cname))
  xobj = x_object_get_sibling(xobj);
#else
  for(; xobj != NULL; xobj = _SBL(xobj))
    {
      TRACE("xobj = %p\n", xobj);
      _name = _GETNM(xobj);
      TRACE("_name = %s, cname = %s\n", _name, cname);

      if(EQN(_name, cname))
        {
          TRACE("EQUAL!!!!!!\n");
          _xobj = xobj;
          break;
        }
      else
        {
          TRACE("NOT EQUAL???!!! '%s' != '%s'\n", _name, cname);
        }
    }
#endif

  if (lock)
    XOBJ_CS_UNLOCK(xparent);

  /*EXIT;*/
  return _xobj;
}

/**
 * @brief x_object_get_child
 * @param xparent
 * @param cname
 * @return
 */
x_object *
x_object_get_child(x_object *xparent, const x_string_t cname)
{
  return __x_object_get_child(xparent, cname, 1);
}

static x_object *
__x_object_get_child_nolock(x_object *xparent, const x_string_t cname)
{
  return __x_object_get_child(xparent, cname, 0);
}

/**
 * Get Child from index
 *
 * @brief x_object_get_child_from_index
 * @param xparent
 * @param ind
 * @return
 */
x_object *
x_object_get_child_from_index(x_object *xparent, int ind)
{
  int i;
  x_object *o;

  XOBJ_CS_LOCK(xparent);

  for (o = TNODE_TO_XOBJ(TREE_NODE(xparent)->sons), i = 0;
      (o != NULL) && (i < ind); o = _SBL(o), i++)
    ;
  XOBJ_CS_UNLOCK(xparent);

  return o;
}

/**
 * Get object's index in a parent
 * @brief x_object_get_index
 * @param xobj
 * @return
 */
int
x_object_get_index(x_object *xobj)
{
  int i = 0;
  x_object *o, *parent;
  tree_node_t *tnode;

  parent = _PARNT(xobj);
  if (!parent)
    return 0;

  XOBJ_CS_LOCK(parent);

  tnode = TREE_NODE(parent);
  tnode = tnode ? tnode->sons : NULL;

  for (o = TNODE_TO_XOBJ(tnode), i = 0;
      (o != NULL); o = _SBL(o), i++)
    {
      if (o == xobj)
        break;
    }

  XOBJ_CS_UNLOCK(parent);

  if (!o)
    i = 0;

  return i;
}

/**
 * Get element by tag name
 *
 * @brief x_object_get_element_by_tag_name
 * @param parent
 * @param name
 * @return
 */
x_object *
x_object_get_element_by_tag_name(const x_object *parent, const x_string_t name)
{
  if (!parent || !name)
    return NULL;
  return _CHLD(parent, name);
}

/**
 * @brief x_object_get_next
 * @param xobj
 * @return
 */
x_object *
x_object_get_next(const x_object *xobj)
{
  tree_node_t *tnode = TREE_NODE(xobj);
  tnode = tnode ? tnode->next : NULL;
  return TNODE_TO_XOBJ(tnode);
}

/**
 * @brief x_object_get_sibling
 * @param xobj
 * @return
 */
x_object *
x_object_get_sibling(const x_object *xobj)
{
  tree_node_t *tnode = TREE_NODE(xobj);
  tnode = tnode ? tnode->sibling : NULL;

  TRACE("xobj(%p)->sibling(%p)\n", xobj, tnode);

  return TNODE_TO_XOBJ(tnode);
}

/**
 * Set xobject attribute
 *
 * @brief x_object_setattr
 * @param xobj
 * @param k
 * @param v
 */
void
x_object_setattr(x_object *xobj, const x_string_t k, const x_string_t v)
{
  /*TRACE("Setting %s=%s\n",k,v);*/
  setattr((KEY) k, (VAL) v, &xobj->attrs);
}

/**
 * @brief x_object_getattr
 * @param xobj
 * @param k
 * @return
 */
x_string_t
x_object_getattr(x_object *xobj, const x_string_t k)
{
  const char *res;
  res = getattr((KEY) k, &xobj->attrs);
  /*  TRACE("%p,k(%p),v(%p)=%s\n",(void *)xobj,(void *)k,(void *)res, res);*/
  return res;
}

/**
 * @brief x_object_set_content
 * @param xobj
 * @param buf
 * @param len
 * @return
 */
int
x_object_set_content(x_object *xobj, const char *buf, u_int32_t len)
{
  x_string_clear(&xobj->content);
  x_string_write(&xobj->content, buf, len);
  return 0;
}

static int
__x_object_destroy(x_object *xobj)
{
  const char *_name = NULL;
  ENTER;

  XOBJ_CS_LOCK(xobj);

  attr_list_clear(&xobj->attrs);
  _name = _GETNM(xobj);
  if (_name) x_free((void *) _name);
#pragma message ("what else to free here ?")

  XOBJ_CS_UNLOCK(xobj);
  XOBJ_CS_RELEASE(xobj);

  /* TODO: free all the rest here */
  x_free(xobj);

  EXIT;
  return 0;
}

/**
 * Frees xml device
 *
 * @brief x_object_destroy_no_cb
 * @param xobj
 * @return
 */
int
x_object_destroy_no_cb(x_object *xobj)
{
  x_object *parent;
  x_object *_this, *prev;

  ENTER;

  if (!xobj)
    return 0;

  parent = _PARNT(xobj);

  if (parent)
    {
      XOBJ_CS_LOCK(parent);

      /* unlink from sibling list */
      for (prev = _this = TNODE_TO_XOBJ(TREE_NODE(parent)->sons); _this; prev =
          _this, _this = _SBL(_this))
        {
          if (_this == xobj)
            {
              if (prev == _this)
                _SBL(_this) ? (TREE_NODE(parent)->sons =
                    TREE_NODE(_this)->sibling) :
                    (TREE_NODE(parent)->sons = NULL);
              else
                _SBL(_this) ? (TREE_NODE(prev)->sibling =
                    TREE_NODE(_this)->sibling) :
                    (TREE_NODE(prev)->sibling = NULL);
              break;
            }
        }
      TREE_NODE(xobj)->sibling = NULL;

      /* unlink from 'next' list */
      for (prev = _this = __x_object_get_child_nolock(parent, _GETNM(xobj));
          _this; prev = _this, _this = _NXT(_this))
        {
          if (_this == xobj)
            {
              if (prev != _this)
                {
                  TREE_NODE(prev)->next = TREE_NODE(x_object_get_next(_this));
                  TREE_NODE(_this)->next = NULL;
                }
              break;
            }
        }

      TREE_NODE(xobj)->next = NULL;

      /* unlink from parent */
      TREE_NODE(xobj)->parent = NULL;

      /* decrement child count */
      TREE_NODE(parent)->child_count--;

      XOBJ_CS_UNLOCK(parent);
    }

  __x_object_destroy(xobj);

  EXIT;

  return 0;
}

/**
 * @brief x_object_destroy
 * @param xobj
 */
void
x_object_destroy(x_object *xobj)
{
  ENTER;

  if (!xobj)
    return;

  /*emit destroy signals*/
  if (xobj->objclass && xobj->objclass->on_release)
    xobj->objclass->on_release(xobj);

  x_object_destroy_no_cb(xobj);

  EXIT;
  return;
}

/**
 * @brief x_object_get_parent
 * @param xobj
 * @return
 */
x_object *
x_object_get_parent(const x_object *xobj)
{
  if (xobj)
    return TNODE_TO_XOBJ(TREE_NODE(xobj)->parent);
  return NULL;
}

static __inline__ void
x_object_set_parent(x_object *child, x_object *parent)
{
  TREE_NODE(child)->parent = TREE_NODE(parent);
}

/**
 * @todo Make default assignment callback's symbol public
 * @brief x_object_default_match_cb
 * @param o
 * @param attrs
 * @return
 */
int
x_object_default_match_cb(x_object *o, x_obj_attr_t *attrs)
{
  ENTER;
  EXIT;
  return 0; /* false */
}

/**
 * API Matching function
 * @brief x_object_match
 * @param o
 * @param attrs
 * @return int TRUE if object matches pattern, FALSE otherwise
 */
int
x_object_match(x_object *o, x_obj_attr_t *attrs)
{
  /*ENTER;*/
  if (o && o->objclass)
    {
      if (o->objclass->match)
        return o->objclass->match(o, attrs);
      else
        return x_object_default_match_cb(o, attrs);
    }

  /*EXIT;*/
  return 0;
}

/**
 * @todo Make default assignment callback's symbol public
 * @brief x_object_default_assign_cb
 * @param o
 * @param attrs
 * @return
 */
x_object *
x_object_default_assign_cb(x_object *o, x_obj_attr_t *attrs)
{
  register x_obj_attr_t *attr;
  ENTER;
  for (attr = attrs->next; attr; attr = attr->next)
    {
      _ASET(o, attr->key, attr->val);
    }
  EXIT;
  return o;
}

/**
 * assigns given attributes list to xobject.
 *
 * @brief x_object_assign
 * @param o
 * @param attrs
 * @return
 */
x_object *
x_object_assign(x_object *o, x_obj_attr_t *attrs)
{
  ENTER;

  if (!attrs)
    {
      EXIT;
      return o;
    }

//  XOBJ_CS_LOCK(o);

  /* touch assignment callback */
  if (o->objclass && o->objclass->on_assign)
    {
      o->objclass->on_assign(o, attrs);
    }
  else
    {
      x_object_default_assign_cb(o, attrs);
    }

  /* increment assignment counter */
  o->assign_count++;

//  XOBJ_CS_UNLOCK(o);


  EXIT;
  return o;
}

/**
 * @brief x_object_vassign
 * @param o
 * @return
 */
#include <stdarg.h>
x_object *
x_object_vassign(x_object *o, ...)
{
    int typ;
    x_string_t key;
    x_string_t val;
    x_obj_attr_t attrs = {0,0,0};
    va_list al;
    ENTER;

    va_start(al,o);
    do
    {
        typ = va_arg(al,int);
        key = va_arg(al,x_string_t);
        val = va_arg(al,x_string_t);

        /* @todo Don't ignore type */
        if(typ && key)
        {
            setattr(key,val,&attrs);
        }
    }
    while(typ != 0 && key != NULL);

    va_end(al);

    _ASGN(o,&attrs);

    attr_list_clear(&attrs);

    EXIT;
    return o;
}

/**
 * @brief x_object_lost_focus
 * @param o
 */
void
x_object_lost_focus(x_object *o)
{
  if (o->objclass && o->objclass->commit)
    o->objclass->commit(o);
}

/**
 * Sends object downstream
 *
 * @param xobj Contributor object
 * @param o Object to be sent
 * @param ctx Send context. A context containing various information \
 * for correct sending. Includes for example hints for packet construction.
 */
int
x_object_send_down(x_object *xobj, x_object *o, x_obj_attr_t *ctx)
{
  x_object *to;
  ENTER;

  for (to = xobj; to && !(to->objclass && to->objclass->tx); to = _PARNT(to)
  )
    ;

  if (to)
    {
      TRACE("Sending to '%s'...\n", to->objclass->cname);
      return to->objclass->tx(to, o, ctx);
    }
  else
    {
      TRACE("Cannot find parent TX for '%s'...\n", xobj->objclass->cname);
    }

  EXIT;
  return -1;
}

/**
 * @brief x_object_getenv
 * @param x
 * @param anam
 * @return
 */
const x_string_t
x_object_getenv(x_object *x, const x_string_t anam)
{
  const char *ret = NULL;
  for (; x; x = _PARNT(x)
  )
    if ((ret = _AGET(x, anam)) != NULL)
      break;
  return ret;
}

/**
 * @brief x_object_ref_put
 * @param o
 */
void
x_object_ref_put(x_object *o, void
(*unref_cb)(x_object *))
{
  if (o && --o->refcount.counter <= 0)
    {
      if (unref_cb)
        unref_cb(o);
      _DEL(o);
    }
}

/**
 * @brief x_object_ref_get
 * @param o
 * @return
 */
x_object *
x_object_ref_get(x_object *o)
{
  ++o->refcount.counter;
  return o;
}

/**
 * @brief x_object_get_child_count
 * @param o
 * @return
 */
int
x_object_get_child_count(x_object *o)
{
  return TREE_NODE(o)->child_count;
}

#if 0
/**
 * This emits events to upper objects in a tree
 * and fires their tree_event_listener() callbacks if
 * they exist
 */
static void
__x_object_emit_subtree_event(x_object *sender, uint32_t events)
  {
    int distance = 0;
    x_object *tmp = _PARNT(sender);

    for (; tmp; tmp = _PARNT(tmp))
      {
        if (tmp->objclass->on_subtree_event)
          {
            if (tmp->objclass->on_subtree_event(tmp, sender, events, ++distance))
            break;
          }
      }
    return;
  }

/**
 * This emits instance specific events to
 * object listeners registered to given object.
 */
static void
__x_object_emit_instance_event(x_object *sender, x_obj_attr_t *diff)
  {
    x_object *listener;
    BUG();
    for (; 0;)
      {
      }
  }
#endif

//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
static int
x_object_listeners_exists(x_object *xobj, x_object *listener)
{
  x_obj_listener_t *head = &xobj->listeners;
  x_obj_attr_t *p;

  for (p = head->next; p; p = p->next)
    {
      TRACE("%p == %p ?\n", p->key, listener);
      if ((uint64_t) (void *) p->key == (uint64_t) (void *) listener)
        {
          return 1;
        }
    }
  return 0;
}

/**
 * @brief x_object_subscribe
 * @param xobj
 * @param listener
 * @return
 */
int
x_object_subscribe(x_object *xobj, x_object *listener)
{
  x_obj_listener_t *head = &xobj->listeners;
  x_obj_listener_t *p;

  _REFGET(xobj);
  _REFGET(listener);

//  TRACE("%p,%p,%p\n", head, head->next, listener);

  if (!x_object_listeners_exists(xobj, listener))
    {
      p = x_malloc(sizeof(x_obj_listener_t));
      p->key = (char *) (void *) listener;
      p->val = (char *) (void *) listener;
      p->next = head->next;
      head->next = p;
    }
  else
    {
      _REFPUT(listener,NULL);
    }

  _REFPUT(xobj,NULL);

  return 0;
}

/**
 * @brief x_object_unsubscribe
 * @param xobj
 * @param listener
 * @return
 */
int
x_object_unsubscribe(x_object *xobj, x_object *listener)
{
    int err = -1;
  x_obj_listener_t *head = &xobj->listeners;
  x_obj_attr_t *p, *prev = NULL;

  _REFGET(xobj);

  for (p = head->next, prev = head; p; prev = p, p = p->next)
    if ((uint64_t) (void *) p->key == (uint64_t) (void *) listener)
      {
        prev->next = p->next;
        p->key = (KEY)0;
        p->val = (VAL)0;
        _REFPUT(listener,NULL);
        x_free(p);
        err = 0;
        break;
      }

_clean_n_exit:
  _REFPUT(xobj,NULL);

  return err;
}

/**
 * @brief x_object_reset_subscription
 * @param xobj
 */
void
x_object_reset_subscription(x_object *xobj)
{
  x_obj_listener_t *head = &xobj->listeners;
  x_obj_attr_t *p, *_p;

  _REFGET(xobj);

  for (p = head->next; p;)
    {
      _p = p->next;
      if(p->key)
      {
          _REFPUT((x_object *)(void *)p->key,NULL);
      }
      x_free(p);
      p = _p;
    }

  _REFPUT(xobj,NULL);

  head->next = NULL;
}

/**
 * @brief x_object_listeners_init
 * @param xobj
 */
void
x_object_listeners_init(x_object *xobj)
{
  x_obj_listener_t *head = &xobj->listeners;
  head->next = NULL;
  head->key = NULL;
  head->val = NULL;
}

/**
 * @brief x_object_sendmsg
 * @param to
 * @param from
 * @param msg
 */
void
x_object_sendmsg(x_object *to, const x_object *from, const x_object *msg)
{
  if (to && to->objclass->rx)
    {
      to->objclass->rx(to, from, msg);
    }
}

/**
 * @brief x_object_sendmsg_multicast
 * @param from
 * @param msg
 */
void
x_object_sendmsg_multicast(x_object *from, const x_object *msg)
{
  x_obj_listener_t *p;
  x_obj_listener_t *head = &from->listeners;

  for (p = head->next; p; p = p->next)
    {
//      XOBJ_CS_LOCK(X_OBJECT(p->key));
      _SNDMSG(X_OBJECT(p->key), from, msg);
//      XOBJ_CS_UNLOCK(X_OBJECT(p->key));
    }
}

/**
 * @brief x_object_open
 * @param ctx
 * @param name
 * @param hints
 * @return
 */
x_object *
x_object_open(x_object *ctx, const x_string_t name, x_obj_attr_t *hints)
{
  x_object *tmp;
  x_object *ret = NULL;
  ENTER;

  tmp = _CHLD(ctx,name);
  if (tmp)
    {
      ret = _FIND(tmp, hints);
    }

  if (!ret)
    {
      ret = _GNEW(name,NULL);
      _INS(ctx, ret);
      _ASGN(ret, hints);
    }

  EXIT;
  return ret;
}

/**
 * @brief Duplicates node at one level
 */
x_object *
x_object_clone(x_object * pattern)
{
  x_object *ret;
  x_object *ctx;
  ret = _GNEW(_GETNM(pattern),NULL);
  ctx = _PARNT(pattern);
  if (ctx)
    _INS(ctx, ret);
  _ASGN(ret, &pattern->attrs);
  return ret;
}

/**
 * @brief x_object_get_cr
 * @param xobj
 * @return
 */
uint32_t
x_object_get_cr(x_object *xobj)
{
  return xobj->cr;
}

/**
 * @brief x_object_set_cr
 * @param xobj
 * @param _cr
 * @return
 */
uint32_t
x_object_set_cr(x_object *xobj, uint32_t _cr)
{
  xobj->cr = _cr;
  return xobj->cr;
}

/**
 * @brief x_object_destroy_tree
 * @param thiz
 */
void
x_object_destroy_tree(x_object *thiz)
{
    x_object *chld;
    x_object *_chld;

    printf("%s():%d: Destroy tree of '%s' refs(%d)\n",
           __FUNCTION__,__LINE__,_GETNM(thiz), thiz->refcount.counter);

    if (thiz->objclass && thiz->objclass->on_tree_destroy)
        thiz->objclass->on_tree_destroy(thiz);

    for (chld = _CHLD(thiz,NULL);chld;)
    {
      printf(">> Destroying '%s' refs(%d)\n",_GETNM(chld),chld->refcount.counter);

      x_object_destroy_tree(chld);

      _chld = chld;
      /* save next */
      chld = _SBL(chld);

      /* drop child */
      _RMOV(_chld);

      printf("<< Destroying '%s' refs(%d)\n",_GETNM(_chld),_chld->refcount.counter);

      /* schedule for deletion */
      _REFPUT(_chld,NULL);
    }

    printf("%s():%d: Destroy tree complete '%s' refs(%d)\n",
           __FUNCTION__,__LINE__,_GETNM(thiz), thiz->refcount.counter);
}

#ifdef XOBJECT_EVENTS
/**
 * Get event domain controller
 * @brief x_object_get_nearest_evdomain
 * @param xobj
 * @return xevent_dom_t pointer
 */
x_object *
x_object_get_nearest_evdomain(x_object *xobj, x_obj_attr_t *hints)
{
    x_object *ret = NULL;
    for (; xobj; xobj = _PARNT(xobj)
         )
        if (xobj->loop != NULL)
        {
            ret = xobj;
            break;
        }
    return ret;
}

/**
 * @todo This should be replaced with event domains subsystem.
 * @brief x_object_add_watcher
 * @param xobj
 * @param watcher
 * @param wtype
 * @return
 */
int
x_object_add_watcher(x_object *xobj, void *watcher, int wtype)
{
    x_object *evdom = NULL;
//    x_object *bus = obj->bus;
    ENTER;

    TRACE("Priority = %d\n", ev_priority(watcher));

    evdom = x_object_get_nearest_evdomain(xobj,NULL);
    if (!evdom)
    {
        BUG();
        return -1;
    }

    switch (wtype)
    {
    case EVT_IO:
        _REFGET(evdom);
        ev_io_start(evdom->loop, (ev_io *) watcher);
        xobj->event_domain = evdom;
        break;

    case EVT_PERIODIC:
        _REFGET(evdom);
        ev_periodic_start(evdom->loop, (ev_periodic *) watcher);
        xobj->event_domain = evdom;
        break;

    case EVT_TIMER:
        BUG();
        break;

    default:
        BUG();
        break;
    };

    EXIT;
}

/**
 * @brief x_object_remove_watcher
 * @param obj
 * @param watcher
 * @param wtype
 */
void
x_object_remove_watcher(x_object *obj, void *watcher, int wtype)
{
  x_object *evdom = obj->event_domain;
  ENTER;

  TRACE("Priority = %d\n", ev_priority(watcher));
  if (!evdom)
  {
      EXIT;
      return;
  }

  if (!evdom->loop)
  {
      EXIT;
      return;
  }

  switch (wtype)
    {
  case EVT_IO:
    ev_io_stop(evdom->loop, (ev_io *) watcher);
    _REFPUT(evdom,NULL);
    break;

  case EVT_PERIODIC:
    ev_periodic_stop(evdom->loop, (ev_periodic *) watcher);
    _REFPUT(evdom,NULL);
    break;

  case EVT_TIMER:
    BUG();
    break;

  default:
    BUG();
    break;
    };

  EXIT;
}
#endif


/**
 * @brief x_object_add_follower Add follower to an object
 * @param thiz
 * @param follower
 * @param slot Numeric ID used by follower to demultiplex writes
 * @return
 */
int
x_object_add_follower(x_object *thiz, x_object *follower, int slot)
{
  x_obj_listener_t *head = &thiz->writeout_vector;
  x_obj_listener_t *p;

  /* @todo Reference counter of this should not be incremented? */
  _REFGET(thiz);
  _REFGET(follower);

  if (!x_object_listeners_exists(thiz, follower))
    {
      p = x_malloc(sizeof(x_obj_listener_t));
      p->key = (char *) (void *) follower;
      p->val = (char *) (void *) follower;
      p->next = head->next;
      head->next = p;
    }
  else
    {
      _REFPUT(follower,NULL);
    }

  _REFPUT(thiz,NULL);

  return 0;
}

int
x_object_del_follower(x_object *thiz, x_object *follower)
{
    int err = -1;
  x_obj_listener_t *head = &thiz->writeout_vector;
  x_obj_attr_t *p, *prev = NULL;

  _REFGET(thiz);

  for (p = head->next, prev = head; p; prev = p, p = p->next)
    if ((uint64_t) (void *) p->key == (uint64_t) (void *) follower)
      {
        prev->next = p->next;
        p->key = (KEY)0;
        p->val = (VAL)0;
        _REFPUT(follower,NULL);
        x_free(p);
        err = 0;
        break;
      }

_clean_n_exit:
  _REFPUT(thiz,NULL);

  return err;
}

/**
 * @brief x_object_write_out
 * @param from
 * @param buf
 * @param len
 * @param attr
 * @return
 * @deprecated
 */
int
x_object_write_out(const x_object *thiz, const void *buf, int len, x_obj_attr_t *hints)
{
    int err = -1;
    x_obj_listener_t *p, *prev = NULL;
    x_obj_listener_t *head = &thiz->writeout_vector;

    /*
     * first write to write_handler (for compitability)
     */
    if (thiz->write_handler)
    {
        err = _WRITE(thiz->write_handler,buf,len,hints);
    }

    for (p = head->next, prev = head; p; prev = p, p = p->next)
    {
        /* XOBJ_CS_LOCK(X_OBJECT(p->key)); */
        if(p->key
                && ((unsigned long)p->key
                    != (unsigned long)thiz->write_handler))
        {
            err = _WRITE(X_OBJECT(p->key),(void *)buf, len, hints);
            x_object_io_update(thiz,&p,&prev,(err<0));
        }
        /* XOBJ_CS_UNLOCK(X_OBJECT(p->key)); */
    }

    return err;
}

/**
 * @brief x_object_writev_out
 * @param from
 * @param iov
 * @param iovcnt
 * @param attr
 * @return
 */
int
x_object_writev_out(const x_object *thiz, const struct iovec *iov, int iovcnt,
                                      x_obj_attr_t *hints)
{
    int err = -1;

    x_obj_listener_t *p, *prev = NULL;
    x_obj_listener_t *head = &thiz->writeout_vector;

    /*
     * first write to write_handler (for compitability)
     */
    if (thiz->write_handler)
    {
        err = _WRITEV(thiz->write_handler,iov,iovcnt,hints);
    }

    for (p = head->next, prev = head; p; prev = p, p = p->next)
    {
        /* XOBJ_CS_LOCK(X_OBJECT(p->key)); */
        if(p->key
                && ((unsigned long)p->key
                    != (unsigned long)thiz->write_handler))
        {
            err = _WRITEV(X_OBJECT(p->key),iov, iovcnt, hints);
            x_object_io_update(thiz,&p,&prev,err);
        }
        /* XOBJ_CS_UNLOCK(X_OBJECT(p->key)); */
    }

    return err;
}

/**
 * @brief x_object_io_update
 * @param thiz
 * @param p
 * @param prev
 * @param err
 */
static __inline__ void
x_object_io_update(x_object *thiz, x_obj_listener_t **p,
                   x_obj_listener_t **prev, int err)
{
    x_object *xobj = X_OBJECT((*p)->key);

    if (!xobj)
    {
        /* remove */
    }
    else if (err < 0)
    {
        fprintf(stderr,"Deleting faulty follower (%d)\n",xobj->io_life_count);
        xobj->io_life_count -= 10;
        if (xobj->io_life_count < -DEFAULT_IOLIFECOUNT)
        {
            (*prev)->next = (*p)->next;
            (*p)->key = (KEY)0;
            (*p)->val = (VAL)0;
            _REFPUT(xobj,NULL);
            _REFPUT(thiz,NULL);
            x_free((*p));
            *p = *prev;
        }
    }
    else
    {
        ++xobj->io_life_count;
    }
}


