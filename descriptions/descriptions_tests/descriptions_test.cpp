#include "testing/testing.hpp"

#include "descriptions/serdes.hpp"

#include "coding/reader.hpp"
#include "coding/writer.hpp"

using namespace descriptions;

UNIT_TEST(Descriptions_SerDes)
{
  DescriptionsCollection data =
    { {100, {{10, "Description of feature 100, language 10."},
             {11, "Описание фичи 100, язык 11."}}},
      {101, {{11, "Описание фичи 101, язык 11."}}},
      {102, {{10, "Description of feature 102, language 10."},
             {11, "Описание фичи 102, язык 11."}}}
    };

  std::vector<uint8_t> buffer;
  {
    Serializer ser(std::move(data));
    MemWriter<decltype(buffer)> writer(buffer);
    ser.Serialize(writer);
  }

  std::string description1;
  std::string description2;
  std::string description3;
  {
    Deserializer des;
    MemReader reader(buffer.data(), buffer.size());
    des.Deserialize(reader, 102, {11, 10}, description1);
    des.Deserialize(reader, 100, {12, 10}, description2);
    des.Deserialize(reader, 101, {12}, description3);
  }

  TEST_EQUAL(description1, "Описание фичи 102, язык 11.", ());
  TEST_EQUAL(description2, "Description of feature 100, language 10.", ());
  TEST_EQUAL(description3, "", ());
}
