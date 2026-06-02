#ifndef CLASSIFIER_H
#define CLASSIFIER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLASSIFIER_NUM_FEATURES 30U
#define CLASSIFIER_NUM_CLASSES 2U

float classifier_predict_proba_class1(const float features[CLASSIFIER_NUM_FEATURES]);
void classifier_predict_proba(const float features[CLASSIFIER_NUM_FEATURES],
                              float probabilities[CLASSIFIER_NUM_CLASSES]);
int32_t classifier_predict(const float features[CLASSIFIER_NUM_FEATURES]);

#ifdef __cplusplus
}
#endif

#endif /* CLASSIFIER_H */
