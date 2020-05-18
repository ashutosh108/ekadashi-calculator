#include "vrata.h"

#include <unordered_set>

namespace vp {

bool operator==(const Vrata &v1, const Vrata &v2)
{
    // non-symmetrical compare for more elegant tests:
    // if v1's paran_start/end is nullopt, then v2's paran_start_end can be anything.
    // But if v1's paran_start/end is set, then v2's one must match.
    return v1.type == v2.type && v1.date == v2.date && v1.paran.type == v2.paran.type &&
            (
                (!v1.paran.paran_start.has_value()) ||
                (v1.paran.paran_start == v2.paran.paran_start)
            ) &&
            (
                (!v1.paran.paran_end.has_value()) ||
                (v1.paran.paran_end == v2.paran.paran_end)
            );
}

bool operator!=(const Vrata &v1, const Vrata &v2)
{
    return !(v1 == v2);
}

std::ostream &operator<<(std::ostream &o, const Vrata &v)
{
    return o << v.type << " on " << v.date;
}

std::ostream &operator<<(std::ostream &o, const Vrata_Type &v)
{
    switch (v) {
    case Vrata_Type::Ekadashi:
        o << "Śuddhā ekādaśī"; break;
    case Vrata_Type::Sandigdha_Ekadashi:
        o << "Ekādaśī fast moved one day forward because ekādaśī tithi starts less than 1/2 ghaṭikā before sūryodaya"; break;
    case Vrata_Type::With_Atirikta_Dvadashi:
        o << "Ekādaśī with atirikta dvādaśī (two days fast)"; break;
    case Vrata_Type::Sandigdha_With_Atirikta_Dvadashi:
        o << "Ekādaśī moved one day forward because ekādaśī tithi starts less than 1/2 ghaṭikā before sūryodaya with atiriktā dvādaśī next day (two days fast)"; break;
    case Vrata_Type::Atirikta_Ekadashi:
        o << "Atiriktā ekādaśī (two days fast)"; break;
    case Vrata_Type::Sandigdha_Atirikta_Ekadashi:
        o << "Atiriktā ekādaśī moved one day forward because ekādaśī tithi starts less than 1/2 ghaṭikā before sūryodaya (two days fast)"; break;
//    default:
//        o << "Vrata#" << static_cast<int>(v);
    }
    return o;
}

const std::vector<std::string> & ekadashi_names()
{
    static const std::vector<std::string> static_names{
        "Pāpamocanī",         // 01
        "Kāmadā",             // 02
        "Varūthinī",          // 03
        "Mohinī",             // 04
        "Aparā",              // 05
        "Nirjalā",            // 06
        "Yoginī",             // 07
        "Śayanī",             // 08
        "Kāmikā",             // 09
        "Pāvitrā",            // 10
        "Ajā",                // 11
        "Pārśva-parivartinī", // 12
        "Indirā",             // 13
        "Pāśāṅkuśā",          // 14
        "Ramā",               // 15
        "Prabodhinī",         // 16
        "Utpattikā",          // 17
        "Mokṣadā",            // 18
        "Saphalā",            // 19
        "Putradā",            // 20
        "Ṣaṭ-tilā",           // 21
        "Jayā",               // 22
        "Vijayā",             // 23
        "Āmalakī",            // 24
        "Kamalā",             // 25
        "Padmā"               // 26
    };
    return static_names;
}

const std::vector<std::string> & ekadashi_names_rus()
{
    static const std::vector<std::string> static_names{
        "Пāпамочанӣ",         // 01
        "Кāмадā",             // 02
        "Варӯтӿинӣ",          // 03
        "Мохинӣ",             // 04
        "Апарā",              // 05
        "Нирџалā",            // 06
        "Йогинӣ",             // 07
        "Щайанӣ",             // 08
        "Кāмикā",             // 09
        "Пāвитрā",            // 10
        "Аџā",                // 11
        "Пāрщва-паривартинӣ", // 12
        "Индирā",             // 13
        "Пāщāӈкущā",          // 14
        "Рамā",               // 15
        "Прабодӿинӣ",         // 16
        "Утпаттикā",          // 17
        "Мокшадā",            // 18
        "Сапӿалā",            // 19
        "Путрадā",            // 20
        "Шат̣тилā",            // 21
        "Џайā",               // 22
        "Виџайā",             // 23
        "Āмалакӣ",            // 24
        "Камалā",             // 25
        "Падмā"               // 26
    };
    return static_names;
}

bool is_atirikta(Vrata_Type type)
{
    return type == Vrata_Type::With_Atirikta_Dvadashi || type == Vrata_Type::Sandigdha_With_Atirikta_Dvadashi
            ||
            type == Vrata_Type::Atirikta_Ekadashi || type == Vrata_Type::Sandigdha_Atirikta_Ekadashi;
}

bool ekadashi_name_rus_is_valid(const std::string &name)
{
    static std::unordered_set<std::string> valid_names = []() {
        std::unordered_set<std::string> v;
        for (const auto & n : ekadashi_names_rus()) {
            v.insert(n);
        }
        v.insert("Шат̣-тилā"); // alternative spelling
        v.insert("Амалакӣ");  // alternative spelling
        return v;
    }();
    return valid_names.find(name) != valid_names.end();
}

date::year_month_day Vrata::local_paran_date()
{
    auto delta = date::days{vp::is_atirikta(type) ? 2 : 1};
    return date::year_month_day{date::sys_days{date} + delta};
}

double_ghatikas Ativrddhatvam::dashami_length() const {
    return ekadashi_start - dashami_start;
}

double_ghatikas Ativrddhatvam::ekadashi_length() const {
    return dvadashi_start - ekadashi_start;
}

double_ghatikas Ativrddhatvam::dvadashi_length() const {
    return trayodashi_start - dvadashi_start;
}

Ativrddhatvam::Ativrddhaadi Ativrddhatvam::ativrddhaadi() const
{
    auto l10 = dashami_length().count();
    auto l11 = ekadashi_length().count();
    auto l12 = dvadashi_length().count();
    auto delta1 = l11 - l10;
    auto delta2 = l12 - l11;
    if (delta1 > 0 && delta2 > 0 && (delta1 >= 4.0 || delta2 >= 4.0)) {
        return Ativrddhaadi::ativrddha;
    }
    if (delta1 > 0 && delta2 > 0 && (delta1 >= 1.0 || delta2 >= 1.0)) {
        return Ativrddhaadi::vrddha;
    }
    if (delta1 < 0 && delta2 < 0) return Ativrddhaadi::hrasva;
            return Ativrddhaadi::samyam;
}

std::ostream &operator<<(std::ostream &s, const Ativrddhatvam::Ativrddhaadi &a)
{
    switch (a) {
    case Ativrddhatvam::Ativrddhaadi::ativrddha:
        s << "ativRddhA";
        break;
    case Ativrddhatvam::Ativrddhaadi::vrddha:
        s << "vRddhA";
        break;
    case Ativrddhatvam::Ativrddhaadi::samyam:
        s << "samyam";
        break;
    case Ativrddhatvam::Ativrddhaadi::hrasva:
        s << "hrasva";
        break;
    }
    return s;
}

} // namespace vp
