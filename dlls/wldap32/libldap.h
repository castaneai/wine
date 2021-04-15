/*
 * Copyright 2021 Hans Leidekker for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* compatible with structures defined in ldap.h */
typedef struct bervalU
{
    unsigned long bv_len;
    char         *bv_val;
} BerValueU;

typedef struct
{
    int   mod_op;
    char *mod_type;
    union
    {
        char           **modv_strvals;
        struct bervalU **modv_bvals;
    } mod_vals;
} LDAPModU;

typedef struct
{
    char          *ldctl_oid;
    struct bervalU ldctl_value;
    char           ldctl_iscritical;
} LDAPControlU;

typedef struct
{
    char *attributeType;
    char *orderingRule;
    int   reverseOrder;
} LDAPSortKeyU;

typedef struct
{
    int             ldvlv_version;
    int             ldvlv_before_count;
    int             ldvlv_after_count;
    int             ldvlv_offset;
    int             ldvlv_count;
    struct bervalU *ldvlv_attrvalue;
    struct bervalU *ldvlv_context;
    void           *ldvlv_extradata;
} LDAPVLVInfoU;

typedef struct timevalU
{
    unsigned long tv_sec;
    unsigned long tv_usec;
} LDAP_TIMEVALU;

#ifndef SASL_CB_LIST_END
#define SASL_CB_LIST_END    0
#define SASL_CB_USER        0x4001
#define SASL_CB_PASS        0x4004
#define SASL_CB_GETREALM    0x4008
#endif

typedef struct sasl_interactU
{
    unsigned long id;
    const char   *challenge;
    const char   *prompt;
    const char   *defresult;
    const void   *result;
    unsigned int  len;
} sasl_interact_tU;

extern void * CDECL wrap_ber_alloc_t(int) DECLSPEC_HIDDEN;
extern void CDECL wrap_ber_bvecfree(struct bervalU **) DECLSPEC_HIDDEN;
extern void CDECL wrap_ber_bvfree(struct bervalU *) DECLSPEC_HIDDEN;
extern unsigned int CDECL wrap_ber_first_element(void *, unsigned int *, char **) DECLSPEC_HIDDEN;
extern int CDECL wrap_ber_flatten(void *, struct bervalU **) DECLSPEC_HIDDEN;
extern void CDECL wrap_ber_free(void *, int) DECLSPEC_HIDDEN;
extern void * CDECL wrap_ber_init(struct bervalU *) DECLSPEC_HIDDEN;
extern unsigned int CDECL wrap_ber_next_element(void *, unsigned int *, char *) DECLSPEC_HIDDEN;
extern unsigned int CDECL wrap_ber_peek_tag(void *, unsigned int *) DECLSPEC_HIDDEN;
extern unsigned int CDECL wrap_ber_skip_tag(void *, unsigned int *) DECLSPEC_HIDDEN;
extern int WINAPIV wrap_ber_printf(void *, char *, ...) DECLSPEC_HIDDEN;
extern int WINAPIV wrap_ber_scanf(void *, char *, ...) DECLSPEC_HIDDEN;

extern int CDECL wrap_ldap_abandon_ext(void *, int, LDAPControlU **, LDAPControlU **) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_add_ext(void *, const char *, LDAPModU **, LDAPControlU **, LDAPControlU **,
                                   ULONG *) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_add_ext_s(void *, const char *, LDAPModU **, LDAPControlU **,
                                     LDAPControlU **) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_compare_ext(void *, const char *, const char *, struct bervalU *, LDAPControlU **,
                                       LDAPControlU **, ULONG *) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_compare_ext_s(void *, const char *, const char *, struct bervalU *, LDAPControlU **,
                                         LDAPControlU **) DECLSPEC_HIDDEN;
extern void CDECL wrap_ldap_control_free(LDAPControlU *) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_count_entries(void *, void *) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_count_references(void *, void *) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_create_sort_control(void *, LDAPSortKeyU **, int, LDAPControlU **) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_create_vlv_control(void *, LDAPVLVInfoU *, LDAPControlU **) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_delete_ext(void *, const char *, LDAPControlU **, LDAPControlU **, ULONG *) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_delete_ext_s(void *, const char *, LDAPControlU **, LDAPControlU **) DECLSPEC_HIDDEN;
extern char * CDECL wrap_ldap_first_attribute(void *, void *, void **) DECLSPEC_HIDDEN;
extern void * CDECL wrap_ldap_first_entry(void *, void *) DECLSPEC_HIDDEN;
extern void * CDECL wrap_ldap_first_reference(void *, void *) DECLSPEC_HIDDEN;
extern void CDECL wrap_ldap_memfree(void *) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_msgfree(void *) DECLSPEC_HIDDEN;
extern char * CDECL wrap_ldap_next_attribute(void *, void *, void *) DECLSPEC_HIDDEN;
extern void * CDECL wrap_ldap_next_entry(void *, void *) DECLSPEC_HIDDEN;
extern void * CDECL wrap_ldap_next_reference(void *, void *) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_parse_result(void *, void *, int *, char **, char **, char ***, LDAPControlU ***,
                                        int) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_result(void *, int, int, struct timevalU *, void **) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_sasl_bind(void *, const char *, const char *, struct bervalU *, LDAPControlU **,
                                     LDAPControlU **, int *) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_sasl_bind_s(void *, const char *, const char *, struct bervalU *, LDAPControlU **,
                                       LDAPControlU **, struct bervalU **) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_sasl_interactive_bind_s(void *, const char *, const char *, LDAPControlU **,
                                                   LDAPControlU **, unsigned int, void *) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_search_ext(void *, const char *, int, const char *, char **, int, LDAPControlU **,
                                      LDAPControlU **, struct timevalU *, int, ULONG *) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_search_ext_s(void *, const char *, int, const char *, char **, int, LDAPControlU **,
                                        LDAPControlU **, struct timevalU *, int, void **) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_unbind_ext(void *, LDAPControlU **, LDAPControlU **) DECLSPEC_HIDDEN;
extern int CDECL wrap_ldap_unbind_ext_s(void *, LDAPControlU **, LDAPControlU **) DECLSPEC_HIDDEN;
extern void CDECL wrap_ldap_value_free_len(struct bervalU **) DECLSPEC_HIDDEN;

struct ldap_funcs
{
    void * (CDECL *ber_alloc_t)(int);
    void (CDECL *ber_bvecfree)(struct bervalU **);
    void (CDECL *ber_bvfree)(struct bervalU *);
    unsigned int (CDECL *ber_first_element)(void *, unsigned int *, char **);
    int (CDECL *ber_flatten)(void *, struct bervalU **);
    void (CDECL *ber_free)(void *, int);
    void * (CDECL *ber_init)(struct bervalU *);
    unsigned int (CDECL *ber_next_element)(void *, unsigned int *, char *);
    unsigned int (CDECL *ber_peek_tag)(void *, unsigned int *);
    unsigned int (CDECL *ber_skip_tag)(void *, unsigned int *);
    int (WINAPIV *ber_printf)(void *, char *, ...);
    int (WINAPIV *ber_scanf)(void *, char *, ...);

    int (CDECL *ldap_abandon_ext)(void *, int, LDAPControlU **, LDAPControlU **);
    int (CDECL *ldap_add_ext)(void *, const char *, LDAPModU **, LDAPControlU **, LDAPControlU **, ULONG *);
    int (CDECL *ldap_add_ext_s)(void *, const char *, LDAPModU **, LDAPControlU **, LDAPControlU **);
    int (CDECL *ldap_compare_ext)(void *, const char *, const char *, struct bervalU *, LDAPControlU **,
                                  LDAPControlU **, ULONG *);
    int (CDECL *ldap_compare_ext_s)(void *, const char *, const char *, struct bervalU *, LDAPControlU **,
                                    LDAPControlU **);
    void (CDECL *ldap_control_free)(LDAPControlU *);
    int (CDECL *ldap_count_entries)(void *, void *);
    int (CDECL *ldap_count_references)(void *, void *);
    int (CDECL *ldap_create_sort_control)(void *, LDAPSortKeyU **, int, LDAPControlU **);
    int (CDECL *ldap_create_vlv_control)(void *, LDAPVLVInfoU *, LDAPControlU **);
    int (CDECL *ldap_delete_ext)(void *, const char *, LDAPControlU **, LDAPControlU **, ULONG *);
    int (CDECL *ldap_delete_ext_s)(void *, const char *, LDAPControlU **, LDAPControlU **);
    char * (CDECL *ldap_first_attribute)(void *, void *, void **);
    void * (CDECL *ldap_first_entry)(void *, void *);
    void * (CDECL *ldap_first_reference)(void *, void *);
    void (CDECL *ldap_memfree)(void *);
    int (CDECL *ldap_msgfree)(void *);
    char * (CDECL *ldap_next_attribute)(void *, void *, void *);
    void * (CDECL *ldap_next_entry)(void *, void *);
    void * (CDECL *ldap_next_reference)(void *, void *);
    int (CDECL *ldap_parse_result)(void *, void *, int *, char **, char **, char ***, LDAPControlU ***, int);
    int (CDECL *ldap_result)(void *, int, int, struct timevalU *, void **);
    int (CDECL *ldap_sasl_bind)(void *, const char *, const char *, struct bervalU *, LDAPControlU **, LDAPControlU **,
                                int *);
    int (CDECL *ldap_sasl_bind_s)(void *, const char *, const char *, struct bervalU *, LDAPControlU **,
                                  LDAPControlU **, struct bervalU **);
    int (CDECL *ldap_sasl_interactive_bind_s)(void *, const char *, const char *, LDAPControlU **, LDAPControlU **,
                                              unsigned int, void *);
    int (CDECL *ldap_search_ext)(void *, const char *, int, const char *, char **, int, LDAPControlU **,
                                 LDAPControlU **, struct timevalU *, int, ULONG *);
    int (CDECL *ldap_search_ext_s)(void *, const char *, int, const char *, char **, int, LDAPControlU **,
                                   LDAPControlU **, struct timevalU *, int, void **);
    int (CDECL *ldap_unbind_ext)(void *, LDAPControlU **, LDAPControlU **);
    int (CDECL *ldap_unbind_ext_s)(void *, LDAPControlU **, LDAPControlU **);
    void (CDECL *ldap_value_free_len)(struct bervalU **);
};

extern int CDECL sasl_interact_cb(void *, unsigned int, void *, void *) DECLSPEC_HIDDEN;

struct ldap_callbacks
{
    int (CDECL *sasl_interact)(void *, unsigned int, void *, void *);
};

extern const struct ldap_funcs *ldap_funcs;